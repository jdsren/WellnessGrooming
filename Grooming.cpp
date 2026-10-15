#include "WG.h"

namespace WG {

namespace {
    std::vector<StyleOpt> g_all[2];
    std::vector<StyleOpt> g_view[2];
    std::string g_raceName;
    std::mutex g_viewMx;
    bool g_dirty3D = false;
    int g_syncCounter = 0;

    RE::BGSHeadPart::HeadPartType PartType(TrackId t) {
        return t == TrackId::Hair ? RE::BGSHeadPart::HeadPartType::kHair : RE::BGSHeadPart::HeadPartType::kFacialHair;
    }
    Settings::Track& Cfg(TrackId t) { return t == TrackId::Hair ? g_cfg.hair : g_cfg.beard; }
    int Ix(TrackId t) { return (int)t; }
    std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

    RE::BGSHeadPart* Current(TrackId t) {
        auto* pl = Player();
        auto* npc = pl ? pl->GetActorBase() : nullptr;
        return npc ? npc->GetCurrentHeadPartByType(PartType(t)) : nullptr;
    }

    bool SameSlot(const StyleSlot& s, RE::BGSHeadPart* hp) {
        if (!hp || s.empty()) return false;
        if (s.hp) return s.hp == hp;
        return false;
    }

    void Put(TrackId t, RE::BGSHeadPart* hp, bool reset3D) {
        auto* pl = Player();
        auto* npc = pl ? pl->GetActorBase() : nullptr;
        if (!npc || !hp) return;
        npc->ChangeHeadPart(hp);
        if (reset3D) g_dirty3D = true;
    }

    float StressFrac() { return StressAvailable() ? clamp01(StressNow() / 100.f) : 0.f; }
}

// ---------------------------------------------------------------- catalog
void BuildCatalog() {
    g_all[0].clear(); g_all[1].clear();
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return;
    for (auto* hp : dh->GetFormArray<RE::BGSHeadPart>()) {
        if (!hp || hp->IsDeleted() || hp->IsExtraPart()) continue;
        if (!hp->flags.any(RE::BGSHeadPart::Flag::kPlayable)) continue;
        TrackId t;
        if (hp->type == RE::BGSHeadPart::HeadPartType::kHair) t = TrackId::Hair;
        else if (hp->type == RE::BGSHeadPart::HeadPartType::kFacialHair) t = TrackId::Beard;
        else continue;
        StyleOpt o;
        o.hp = hp;
        o.formID = hp->GetLocalFormID();
        auto* file = hp->GetFile(0);
        o.plugin = file ? std::string(file->GetFilename()) : "";
        if (o.plugin.empty()) continue;
        o.edid = hp->GetFormEditorID() ? hp->GetFormEditorID() : "";
        const char* nm = hp->GetName();
        o.name = (nm && nm[0]) ? nm : o.edid;
        if (o.name.empty()) continue;
        o.male = hp->flags.any(RE::BGSHeadPart::Flag::kMale);
        o.female = hp->flags.any(RE::BGSHeadPart::Flag::kFemale);
        o.lower = Lower(o.name + " " + o.edid + " " + o.plugin);
        g_all[Ix(t)].push_back(std::move(o));
    }
    for (auto& v : g_all)
        std::sort(v.begin(), v.end(), [](const StyleOpt& a, const StyleOpt& b) {
            if (a.plugin != b.plugin) return Lower(a.plugin) < Lower(b.plugin);
            return a.name < b.name;
        });
    Log(std::format("style catalog: {} hair styles, {} beards", g_all[0].size(), g_all[1].size()));
}

void RefreshCatalogFilter() {
    auto* pl = Player();
    auto* race = pl ? pl->GetRace() : nullptr;
    const bool fem = PlayerIsFemale();
    g_raceName = (race && race->GetName()) ? race->GetName() : "";
    std::lock_guard<std::mutex> lk(g_viewMx);
    for (int t = 0; t < 2; ++t) {
        g_view[t].clear();
        for (auto& o : g_all[t]) {
            if (fem ? !o.female : !o.male) continue;
            if (race && o.hp->validRaces && !o.hp->validRaces->HasForm(race)) continue;
            g_view[t].push_back(o);
        }
    }
}
std::mutex& CatalogMutex() { return g_viewMx; }
const std::vector<StyleOpt>& Catalog(TrackId t) { return g_view[Ix(t)]; }
std::string CatalogRaceName() { return g_raceName; }

// ---------------------------------------------------------------- slots
void ResolveSlots() {
    for (int t = 0; t < 2; ++t) {
        for (auto& s : g_st.slots[t]) {
            s.hp = nullptr;
            if (s.empty()) continue;
            s.hp = skyrim_cast<RE::BGSHeadPart*>(LookupSpec(s.plugin, s.local));
            if (!s.hp) Log("style not found (mod removed?): " + s.name + " from " + s.plugin);
        }
        g_st.track[t].count = SlotCount((TrackId)t);
        g_st.track[t].stage = std::clamp(g_st.track[t].stage, 0, std::max(0, g_st.track[t].count - 1));
    }
    if (!g_st.shaved.empty()) g_st.shaved.hp = skyrim_cast<RE::BGSHeadPart*>(LookupSpec(g_st.shaved.plugin, g_st.shaved.local));
}

int SlotCount(TrackId t) {
    int n = 0;
    for (auto& s : g_st.slots[Ix(t)]) { if (s.empty()) break; ++n; }
    return n;
}

static void Recount(TrackId t) {
    auto& tr = g_st.track[Ix(t)];
    tr.count = SlotCount(t);
    tr.stage = std::clamp(tr.stage, 0, std::max(0, tr.count - 1));
}

bool SetSlot(TrackId t, int idx, const StyleOpt& o) {
    if (idx < 0 || idx >= kMaxStages || !o.hp) return false;
    auto& s = g_st.slots[Ix(t)][idx];
    s.plugin = o.plugin; s.local = o.formID; s.name = o.name; s.hp = o.hp;
    Recount(t);
    return true;
}
void ClearSlot(TrackId t, int idx) {
    if (idx < 0 || idx >= kMaxStages) return;
    auto& v = g_st.slots[Ix(t)];
    // remove and close the gap so the list stays consecutive
    for (int i = idx; i < kMaxStages - 1; ++i) v[i] = v[i + 1];
    v[kMaxStages - 1] = StyleSlot{};
    Recount(t);
}
void MoveSlot(TrackId t, int idx, int dir) {
    auto& v = g_st.slots[Ix(t)];
    int j = idx + dir;
    if (idx < 0 || j < 0 || idx >= kMaxStages || j >= kMaxStages) return;
    if (v[idx].empty() || v[j].empty()) return;
    std::swap(v[idx], v[j]);
    Recount(t);
}
void RecordCurrent(TrackId t, int idx) {
    auto* hp = Current(t);
    if (!hp) return;
    auto* file = hp->GetFile(0);
    StyleOpt o;
    o.hp = hp; o.formID = hp->GetLocalFormID();
    o.plugin = file ? std::string(file->GetFilename()) : "";
    o.name = (hp->GetName() && hp->GetName()[0]) ? hp->GetName() : (hp->GetFormEditorID() ? hp->GetFormEditorID() : "current");
    if (!o.plugin.empty()) SetSlot(t, idx, o);
}

// ---------------------------------------------------------------- applying
void PreviewSlot(TrackId t, int idx) {
    if (idx < 0 || idx >= kMaxStages) return;
    auto* hp = g_st.slots[Ix(t)][idx].hp;
    if (hp) { Put(t, hp, true); }
}

void ApplyStage(TrackId t, bool force) {
    auto& tr = g_st.track[Ix(t)];
    if (tr.count <= 0) return;
    tr.stage = std::clamp(tr.stage, 0, tr.count - 1);
    auto* hp = g_st.slots[Ix(t)][tr.stage].hp;
    if (!hp) return;
    if (!force && Current(t) == hp) return;
    Put(t, hp, true);
    g_st.externalChange[Ix(t)] = false;
}

void BeginStartApply() {
    for (int i = 0; i < 2; ++i) {
        auto t = (TrackId)i;
        if (!Cfg(t).enabled) continue;
        if (SlotCount(t) == 0) RecordCurrent(t, 0);   // no list yet: today's look becomes style 1
        auto& tr = g_st.track[i];
        tr.count = SlotCount(t);
        tr.stage = std::clamp(Cfg(t).startStage - 1, 0, std::max(0, tr.count - 1));
        tr.progress = 0.f;
        g_st.externalChange[i] = false;
        if (tr.count > 0) ApplyStage(t, true);
    }
}

// ---------------------------------------------------------------- growth
void AdvanceGroomingDays(float days) {
    if (days <= 0.f) return;
    const float f = GrowthBodyFactor(HungerLevel01(), ThirstLevel01(), StressFrac(), true);
    for (int i = 0; i < 2; ++i) {
        auto t = (TrackId)i;
        auto& c = Cfg(t);
        auto& tr = g_st.track[i];
        if (!c.enabled || tr.count <= 1 || g_st.externalChange[i]) continue;
        GrowthParams gp;
        gp.defaultDays = c.days;
        gp.perStage = c.perStage;
        gp.speed = c.speedPct / 100.f;
        const int gained = AdvanceGrowth(tr, gp, days * (c.bodyFactor ? f : 1.f));
        if (gained > 0) {
            ApplyStage(t, false);
            if (c.notify) Notify(i == 0 ? "Your hair has grown longer." : "Your beard has grown longer.");
            if (g_cfg.stressFromGrooming && i == 0 && tr.stage >= tr.count - 1 && tr.count > 3) AddStress(1.0f, "overgrown hair");
        }
    }
}

// ---------------------------------------------------------------- tools
bool HasTool(TrackId t) {
    if (!Cfg(t).requireTool) return true;
    auto* pl = Player();
    if (!pl) return false;
    if (auto* sc = GetForm(FormKey::Scissors)) {
        if (auto* bo = sc->As<RE::TESBoundObject>()) if (pl->GetItemCount(bo) > 0) return true;
    }
    std::vector<std::string> words;
    {
        std::stringstream ss(g_cfg.toolWords);
        std::string w;
        while (std::getline(ss, w, ',')) {
            size_t a = w.find_first_not_of(" \t"), b = w.find_last_not_of(" \t");
            if (a != std::string::npos) words.push_back(Lower(w.substr(a, b - a + 1)));
        }
    }
    if (words.empty()) return false;
    bool found = false;
    auto inv = pl->GetInventory([&](RE::TESBoundObject& o) {
        if (found) return false;
        auto ft = o.GetFormType();
        if (ft != RE::FormType::Misc && ft != RE::FormType::Weapon && ft != RE::FormType::Armor) return false;
        const char* nm = o.GetName();
        if (!nm) return false;
        const auto ln = Lower(nm);
        for (auto& w : words) if (ln.find(w) != std::string::npos) { found = true; return true; }
        return false;
    });
    return found || !inv.empty();
}

bool Trim(TrackId t, int steps, bool toStage, int stage) {
    auto& c = Cfg(t);
    auto& tr = g_st.track[Ix(t)];
    const char* what = t == TrackId::Hair ? "hair" : "beard";
    if (!g_st.active) { Notify("Wellness Grooming is not started."); return false; }
    if (tr.count <= 1) { Notify(std::string("No ") + what + " styles are set up in the menu."); return false; }
    if (!HasTool(t)) { Notify(std::string("You need scissors or shears to cut your ") + what + "."); return false; }
    if (GameBusy() && !toStage) { /* from menu buttons is fine too */ }
    int removed = 0;
    if (toStage) {
        const int target = std::clamp(stage, 0, tr.stage);
        removed = tr.stage - target;
        tr.stage = target; tr.progress = 0.f;
    } else {
        removed = TrimBack(tr, steps);
    }
    if (removed <= 0) { Notify(std::string("Your ") + what + " is already as short as it can be."); return false; }
    ApplyStage(t, true);
    g_st.lastCutDays[Ix(t)] = DaysNow();
    Notify(std::string("You trim your ") + what + ".");
    if (g_cfg.stressFromGrooming) AddStress(-2.0f, "trimmed");
    return true;
}

bool Shave() {
    auto& tr = g_st.track[1];
    if (!g_st.active) { Notify("Wellness Grooming is not started."); return false; }
    if (tr.count <= 0) { Notify("No beard styles are set up in the menu."); return false; }
    if (!HasTool(TrackId::Beard)) { Notify("You need scissors, shears or a razor to shave."); return false; }
    if (tr.stage == 0) { Notify("You are already clean shaven."); return false; }
    tr.stage = 0; tr.progress = 0.f;
    if (g_st.shaved.hp) Put(TrackId::Beard, g_st.shaved.hp, true);
    else ApplyStage(TrackId::Beard, true);
    g_st.lastCutDays[1] = DaysNow();
    Notify("You shave your face clean.");
    if (g_cfg.stressFromGrooming) AddStress(-2.0f, "shaved");
    return true;
}

std::string CurrentStyleName(TrackId t) {
    auto* hp = Current(t);
    if (!hp) return "(none)";
    const char* n = hp->GetName();
    if (n && n[0]) return n;
    return hp->GetFormEditorID() ? hp->GetFormEditorID() : "(unnamed)";
}

// ---------------------------------------------------------------- outside changes (barbers, racemenu, other mods)
void SyncExternalChanges() {
    if (!g_st.active) return;
    for (int i = 0; i < 2; ++i) {
        auto t = (TrackId)i;
        auto& tr = g_st.track[i];
        if (!Cfg(t).enabled || tr.count <= 0) continue;
        auto* cur = Current(t);
        if (!cur) continue;
        if (SameSlot(g_st.slots[i][tr.stage], cur)) { g_st.externalChange[i] = false; continue; }
        int match = -1;
        for (int s = 0; s < tr.count; ++s) if (SameSlot(g_st.slots[i][s], cur)) { match = s; break; }
        if (match >= 0) {            // somebody picked one of "our" styles: continue growing from there
            tr.stage = match; tr.progress = 0.f;
            g_st.externalChange[i] = false;
            if (g_cfg.debugLog) Log(std::string("style changed outside, now at stage ") + std::to_string(match + 1));
        } else {
            if (!g_st.externalChange[i]) Log("a style outside the list is worn, growth paused for " + std::string(i == 0 ? "hair" : "beard"));
            g_st.externalChange[i] = true;
        }
    }
}

void TickGrooming() {
    if (g_dirty3D) {
        g_dirty3D = false;
        if (auto* pl = Player()) pl->DoReset3D(false);
    }
    if (++g_syncCounter >= 5 && !ReliefActive()) { g_syncCounter = 0; SyncExternalChanges(); }
}

}  // namespace WG
