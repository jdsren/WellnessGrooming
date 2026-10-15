#include "WG.h"

namespace WG {

SaveState g_st;

namespace {
    constexpr std::uint32_t kRecState = 'WGST';
    constexpr std::uint32_t kVersion = 1;

    std::atomic<bool> g_loaded{false};       // a save is loaded and the player exists
    std::atomic<bool> g_tickQueued{false};
    std::atomic<bool> g_run{true};
    std::chrono::steady_clock::time_point g_lastReal = std::chrono::steady_clock::now();
    float g_slow = 0.f;

    template <class T> void W(SKSE::SerializationInterface* a, const T& v) { a->WriteRecordData(&v, sizeof(T)); }
    template <class T> bool R(SKSE::SerializationInterface* a, T& v) { return a->ReadRecordData(&v, sizeof(T)) == sizeof(T); }
    void WS(SKSE::SerializationInterface* a, const std::string& s) {
        std::uint32_t n = (std::uint32_t)s.size();
        W(a, n);
        if (n) a->WriteRecordData(s.data(), n);
    }
    bool RS(SKSE::SerializationInterface* a, std::string& s) {
        std::uint32_t n = 0;
        if (!R(a, n) || n > 4096) return false;
        s.assign(n, '\0');
        return n == 0 || a->ReadRecordData(s.data(), n) == n;
    }
    void WSlot(SKSE::SerializationInterface* a, const StyleSlot& s) { WS(a, s.plugin); W(a, s.local); WS(a, s.name); }
    bool RSlot(SKSE::SerializationInterface* a, StyleSlot& s) {
        s = StyleSlot{};
        return RS(a, s.plugin) && R(a, s.local) && RS(a, s.name);
    }

    // ---- hotkeys
    struct KeySink : RE::BSTEventSink<RE::InputEvent*> {
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* e, RE::BSTEventSource<RE::InputEvent*>*) override {
            if (!e || !g_st.active || GameBusy()) return RE::BSEventNotifyControl::kContinue;
            for (auto* ev = *e; ev; ev = ev->next) {
                auto* b = ev->AsButtonEvent();
                if (!b || ev->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !b->IsDown()) continue;
                const int k = (int)b->GetIDCode();
                auto& c = g_cfg;
                if (k == 0) continue;
                if (k == c.keyBladder) Post([] { StartRelief(ReliefKind::Bladder, false); });
                else if (k == c.keyBowel) Post([] { StartRelief(ReliefKind::Bowel, false); });
                else if (k == c.keyBrush) Post([] { BrushTeeth(); });
                else if (k == c.keyHair) Post([] { Trim(TrackId::Hair, g_cfg.hair.trimSteps); });
                else if (k == c.keyBeard) Post([] { Trim(TrackId::Beard, g_cfg.beard.trimSteps); });
                else if (k == c.keyStatus) Post([] {
                    Notify(std::format("Bladder {:.0f}%  Bowel {:.0f}%  Teeth {:.0f}% dirty",
                        100.f * bladderFrac(g_st.needs, g_cfg.needs), 100.f * bowelFrac(g_st.needs), 100.f * g_st.needs.oral));
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    KeySink g_keySink;

    void Tick();
    void QueueTick() {
        bool exp = false;
        if (!g_tickQueued.compare_exchange_strong(exp, true)) return;
        Post([] { Tick(); g_tickQueued = false; });
    }

    void Tick() {
        if (!g_loaded) return;
        auto* pl = Player();
        if (!pl || !pl->Is3DLoaded()) return;

        const auto nowReal = std::chrono::steady_clock::now();
        float dtReal = std::chrono::duration<float>(nowReal - g_lastReal).count();
        g_lastReal = nowReal;
        dtReal = clampf(dtReal, 0.f, 2.f);

        UpdateRelief(dtReal);
        g_slow += dtReal;
        if (g_slow < 1.0f) return;   // everything below runs about once per real second
        g_slow = 0.f;

        const float now = DaysNow();
        if (g_st.lastDays < 0.f) g_st.lastDays = now;
        float dd = now - g_st.lastDays;
        g_st.lastDays = now;
        if (dd < 0.f) dd = 0.f;
        dd = std::min(dd, 5.f);

        if (g_settingsDirty) SaveSettings();
        TickGrooming();
        if (!g_st.active) return;
        RollDay(g_st.day, (int)now);
        if (g_st.paused) return;

        const float hours = dd * 24.f;
        if (hours > 0.f) {
            StepContext ctx;
            ctx.hydration = HydrationFactor();
            ctx.sleeping = hours >= 2.f || (pl->AsActorState() && pl->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping);
            ctx.exertion = (pl->AsActorState() && pl->AsActorState()->IsSprinting()) ? 0.4f : 0.f;
            StepNeeds(g_st.needs, g_cfg.needs, hours, ctx);
            OnNeedsTick(hours);
            AdvanceGroomingDays(dd);
        }
        if (!g_cfg.oralEnabled) g_st.needs.oral = 0.f;
    }

    void ThreadMain() {
        while (g_run) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (g_loaded) QueueTick();
        }
    }
}

// ---------------------------------------------------------------- helpers
RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }
void Post(std::function<void()> fn) {
    if (auto* t = SKSE::GetTaskInterface()) t->AddTask(std::move(fn));
}
float DaysNow() {
    auto* c = RE::Calendar::GetSingleton();
    return c ? c->GetDaysPassed() : 0.f;
}
bool GameBusy() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return true;
    if (ui->GameIsPaused()) return true;
    for (auto* m : {"Loading Menu", "MainMenu", "Journal Menu", "InventoryMenu", "MapMenu", "MessageBoxMenu", "Dialogue Menu",
                    "BarterMenu", "ContainerMenu", "GiftMenu", "Sleep/Wait Menu", "Console", "TweenMenu", "LevelUp Menu", "Book Menu",
                    "Lockpicking Menu", "RaceSex Menu", "MagicMenu", "StatsMenu", "FavoritesMenu", "CustomMenu"})
        if (ui->IsMenuOpen(m)) return true;
    return false;
}
void SetPlayerControls(bool enable) {
    using UE = RE::ControlMap::UEFlag;
    auto* cm = RE::ControlMap::GetSingleton();
    if (!cm) return;
    const auto all = std::to_underlying(UE::kMovement) | std::to_underlying(UE::kFighting) | std::to_underlying(UE::kSneaking) |
                     std::to_underlying(UE::kActivate) | std::to_underlying(UE::kJumping) | std::to_underlying(UE::kPOVSwitch) |
                     std::to_underlying(UE::kWheelZoom);
    cm->ToggleControls(static_cast<UE>(all), enable);
}
void Notify(const std::string& msg) {
    if (!g_cfg.notifications) return;
    RE::DebugNotification(msg.c_str());
}
void Log(const std::string& msg) {
    logger::info("{}", msg);
}
bool PlayerIsFemale() {
    auto* pl = Player();
    auto* npc = pl ? pl->GetActorBase() : nullptr;
    return npc && npc->IsFemale();
}

void ResetState() {
    g_st = SaveState{};
}

float StressNow() {
    auto* g = skyrim_cast<RE::TESGlobal*>(GetForm(FormKey::StressTotal));
    return g ? g->value : 0.f;
}
bool StressAvailable() {
    if (!g_cfg.stressLink) return false;
    if (!GetForm(FormKey::StressTotal)) return false;
    if (auto* en = skyrim_cast<RE::TESGlobal*>(GetForm(FormKey::StressEnabled))) if (en->value == 0.f) return false;
    return true;
}
void AddStress(float delta, const char* reason) {
    if (!StressAvailable() || delta == 0.f) return;
    auto* g = skyrim_cast<RE::TESGlobal*>(GetForm(FormKey::StressTotal));
    if (!g) return;
    const float before = g->value;
    g->value = clampf(before + delta * g_cfg.stressScale, 0.f, 100.f);
    if (g_cfg.debugLog) Log(std::format("stress {:+.2f} ({}) -> {:.1f}", delta, reason ? reason : "", g->value));
}

void BrushTeeth() {
    if (!g_st.active) { Notify("Wellness Grooming is not started."); return; }
    if (g_st.needs.oral < 0.05f) { Notify("Your teeth are already clean."); return; }
    g_st.needs.oral = 0.f;
    RollDay(g_st.day, (int)DaysNow());
    g_st.day.brushed++;
    g_st.lastBrushDays = DaysNow();
    Notify("You scrub your teeth clean.");
    if (g_cfg.stressFromGrooming) AddStress(-1.5f, "brushed teeth");
}

// ---------------------------------------------------------------- control
void StartMod() {
    if (g_st.active) return;
    ResetNeeds();
    g_st.active = true;
    g_st.paused = false;
    g_st.lastDays = DaysNow();
    g_st.day = DayStats{};
    RollDay(g_st.day, (int)DaysNow());
    BeginStartApply();
    Notify("Wellness Grooming started.");
}
void StopMod() {
    CancelRelief("stopped");
    g_st.active = false;
    Notify("Wellness Grooming stopped. Your current look stays as it is.");
}
void PauseMod(bool pause) {
    g_st.paused = pause;
    g_st.lastDays = DaysNow();
}
void ResetNeeds() {
    g_st.needs = NeedsState{};
    g_st.fullHours = 0.f;
    g_st.lastUrgency[0] = g_st.lastUrgency[1] = g_st.lastUrgency[2] = 0;
}
void ReloadFromMenu() {
    LoadSettings();
    ResolveAllForms();
    RefreshCatalogFilter();
    ResolveSlots();
}

// ---------------------------------------------------------------- lifecycle
void Install() {
    LoadSettings();
    // write the file once so that players find every option
    if (!std::filesystem::exists(SettingsPath())) SaveSettings();
    ResolveAllForms();
    BuildCatalog();
    RegisterIntake();
    RegisterUI();
    if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) dm->AddEventSink(&g_keySink);
    Log("installed");
}

void OnLoaded(bool newGame) {
    if (newGame) ResetState();
    // never leave the controls locked by a previous session
    CancelRelief("load");
    SetPlayerControls(true);
    RefreshCatalogFilter();
    ResolveSlots();
    g_st.lastDays = DaysNow();
    if (g_st.active && g_cfg.pauseOnLoad) g_st.paused = true;
    g_lastReal = std::chrono::steady_clock::now();
    g_loaded = true;
    Post([] { SyncExternalChanges(); });
}

void Revert() {
    g_loaded = false;
    CancelRelief("revert");
    ResetState();
}

void SaveTo(SKSE::SerializationInterface* a) {
    if (!a->OpenRecord(kRecState, kVersion)) return;
    auto& s = g_st;
    W(a, s.active); W(a, s.paused); W(a, s.lastDays);
    W(a, s.needs); W(a, s.day);
    for (int t = 0; t < 2; ++t) {
        W(a, s.track[t].count); W(a, s.track[t].stage); W(a, s.track[t].progress);
        for (auto& sl : s.slots[t]) WSlot(a, sl);
    }
    WSlot(a, s.shaved);
    for (float f : s.lastNotifyDays) W(a, f);
    W(a, s.lastVoidDays); W(a, s.lastBowelDays); W(a, s.lastBrushDays);
    W(a, s.lastCutDays[0]); W(a, s.lastCutDays[1]);
    W(a, s.fullHours);
    W(a, s.externalChange[0]); W(a, s.externalChange[1]);
}

void LoadFrom(SKSE::SerializationInterface* a) {
    std::uint32_t type, version, length;
    ResetState();
    while (a->GetNextRecordInfo(type, version, length)) {
        if (type != kRecState || version != kVersion) continue;
        SaveState s;
        bool ok = R(a, s.active) && R(a, s.paused) && R(a, s.lastDays) && R(a, s.needs) && R(a, s.day);
        for (int t = 0; ok && t < 2; ++t) {
            ok = R(a, s.track[t].count) && R(a, s.track[t].stage) && R(a, s.track[t].progress);
            for (auto& sl : s.slots[t]) ok = ok && RSlot(a, sl);
        }
        ok = ok && RSlot(a, s.shaved);
        for (float& f : s.lastNotifyDays) ok = ok && R(a, f);
        ok = ok && R(a, s.lastVoidDays) && R(a, s.lastBowelDays) && R(a, s.lastBrushDays) && R(a, s.lastCutDays[0]) && R(a, s.lastCutDays[1]) &&
             R(a, s.fullHours) && R(a, s.externalChange[0]) && R(a, s.externalChange[1]);
        if (ok) g_st = s;
        else Log("could not read the saved state, starting clean");
    }
}

void StartTickThread() {
    static bool started = false;
    if (started) return;
    started = true;
    std::thread(ThreadMain).detach();
}

}  // namespace WG
