#include "WG.h"
#include "third_party/SKSEMenuFramework.h"

namespace WG {

using namespace ImGuiMCP;

namespace {
    bool g_ui = false;
    int g_pickSlot[2] = {-1, -1};
    char g_filter[2][64] = {"", ""};
    char g_specBuf[(int)FormKey::COUNT][96];
    bool g_specInit = false;
    std::string g_flash;
    float g_flashUntil = 0.f;

    const ImVec4 kGood{0.45f, 0.85f, 0.45f, 1.f}, kWarn{0.95f, 0.75f, 0.30f, 1.f}, kBad{0.95f, 0.40f, 0.35f, 1.f}, kDim{0.65f, 0.65f, 0.70f, 1.f};

    void Dirty() { g_settingsDirty = true; }
    bool Chk(const char* l, bool& v, const char* tip = nullptr) {
        bool r = Checkbox(l, &v);
        if (r) Dirty();
        if (tip && IsItemHovered()) SetTooltip("%s", tip);
        return r;
    }
    bool SlF(const char* l, float& v, float lo, float hi, const char* fmt = "%.2f", const char* tip = nullptr) {
        bool r = SliderFloat(l, &v, lo, hi, fmt);
        if (r) Dirty();
        if (tip && IsItemHovered()) SetTooltip("%s", tip);
        return r;
    }
    bool SlI(const char* l, int& v, int lo, int hi, const char* fmt = "%d", const char* tip = nullptr) {
        bool r = SliderInt(l, &v, lo, hi, fmt);
        if (r) Dirty();
        if (tip && IsItemHovered()) SetTooltip("%s", tip);
        return r;
    }
    bool Cmb(const char* l, int& v, const char* items) {
        bool r = Combo(l, &v, items);
        if (r) Dirty();
        return r;
    }
    void Bar(const char* label, float frac, const char* overlay = nullptr) {
        Text("%s", label);
        SameLine(150.f);
        ProgressBar(clamp01(frac), ImVec2(260.f, 0.f), overlay);
    }
    const char* Lvl(int l) {
        static const char* n[] = {"fine", "noticeable", "urgent", "desperate", "cannot hold"};
        return n[std::clamp(l, 0, 4)];
    }
    void Do(std::function<void()> fn) { Post(std::move(fn)); }

    // ------------------------------------------------------------ pages
    void __stdcall PageStatus() {
        auto& s = g_st;
        if (!Player() || !Player()->Is3DLoaded()) { Text("Load a game first."); return; }
        SeparatorText("Wellness Grooming");
        if (!s.active) {
            TextWrapped("The mod is switched off. Nothing changes your needs, hair or beard until you press Start. "
                        "Set up your hair and beard styles first (Hair and Beard pages), then press Start: the first style in each list is applied and growth begins.");
            if (Button("Start Wellness Grooming", ImVec2(260.f, 34.f))) Do([] { StartMod(); });
        } else {
            TextColored(s.paused ? kWarn : kGood, s.paused ? "Paused" : "Running");
            SameLine(); 
            if (Button(s.paused ? "Resume" : "Pause")) { const bool p = !s.paused; Do([p] { PauseMod(p); }); }
            SameLine();
            if (Button("Reset needs")) Do([] { ResetNeeds(); });
            SameLine();
            if (Button("Stop")) Do([] { StopMod(); });
        }
        Spacing();
        SeparatorText("Body");
        const float bl = bladderFrac(s.needs, g_cfg.needs), bw = bowelFrac(s.needs);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0f%%  (%s)", bl * 100.f, Lvl(UrgencyLevel(bl, g_cfg.urge)));
        Bar("Bladder", bl, buf);
        std::snprintf(buf, sizeof buf, "%.0f%%  (%s)", bw * 100.f, Lvl(UrgencyLevel(bw, g_cfg.urge)));
        Bar("Bowel", bw, buf);
        std::snprintf(buf, sizeof buf, "%.0f%% plaque", s.needs.oral * 100.f);
        Bar("Teeth", s.needs.oral, buf);
        TextColored(kDim, "In the stomach: %.0f ml of fluid   On its way down: %.0f%% of a bowel load", s.needs.stomachFluid, (s.needs.transit0 + s.needs.transit1) * 100.f);
        TextColored(kDim, "Today: %.0f ml drunk (goal %.0f)   %.1f meals (goal %.1f)   %d x pee   %d x bowel   %d x teeth",
                    s.day.fluidMl, g_cfg.dailyFluidGoalMl, s.day.portions, g_cfg.dailyFoodGoal, s.day.voids, s.day.bowel, s.day.brushed);
        if (FormPresent(FormKey::LsThirst))
            TextColored(kDim, "Last Seed: hunger %.0f%%  thirst %.0f%%  -> urine production x%.2f", HungerLevel01() * 100.f, ThirstLevel01() * 100.f, HydrationFactor());
        if (StressAvailable()) TextColored(kDim, "Stress and Fear: stress %.0f", StressNow());

        SeparatorText("Hair and beard");
        for (int i = 0; i < 2; ++i) {
            auto& tr = s.track[i];
            const char* n = i == 0 ? "Hair" : "Beard";
            if (!(i == 0 ? g_cfg.hair.enabled : g_cfg.beard.enabled)) { Text("%s: off", n); continue; }
            if (tr.count == 0) { TextColored(kWarn, "%s: no styles chosen yet", n); continue; }
            Text("%s: style %d of %d  (%s)", n, tr.stage + 1, tr.count, s.slots[i][std::clamp(tr.stage, 0, kMaxStages - 1)].name.c_str());
            if (s.externalChange[i]) { SameLine(); TextColored(kWarn, "growth paused: another style is worn"); }
            else if (!tr.atMax()) { std::snprintf(buf, sizeof buf, "%.0f%% to the next style", tr.progress * 100.f); Bar("", tr.progress, buf); }
        }

        SeparatorText("Do it now");
        if (Button("Pee")) Do([] { StartRelief(ReliefKind::Bladder, false); });
        SameLine(); if (Button("Bowel")) Do([] { StartRelief(ReliefKind::Bowel, false); });
        SameLine(); if (Button("Brush teeth")) Do([] { BrushTeeth(); });
        if (Button("Trim hair")) Do([] { Trim(TrackId::Hair, g_cfg.hair.trimSteps); });
        SameLine(); if (Button("Trim beard")) Do([] { Trim(TrackId::Beard, g_cfg.beard.trimSteps); });
        SameLine(); if (Button("Shave")) Do([] { Shave(); });
        TextColored(kDim, "Relief: %s.   Close this menu first - the character cannot act while it is open.", ReliefStatus().c_str());
        if (ReliefActive() || OriginalAnimationsSeen() || BuiltInAnimationSeen())
            TextColored(kDim, "Animations so far: original mods %s, built-in %s", OriginalAnimationsSeen() ? "used" : "not used", BuiltInAnimationSeen() ? "used" : "not used");
    }

    void __stdcall PageBathroom() {
        auto& c = g_cfg;
        SeparatorText("Body model");
        TextWrapped("Defaults follow an adult body: a bladder of about 450 ml, roughly 1.5 to 2.5 litres of urine per day when you drink and eat normally, and one bowel movement a day.");
        SlF("Bladder capacity (ml)", c.needs.bladderCapMl, 150.f, 1000.f, "%.0f", "Volume at which the bladder is 100% full");
        SlF("Fluid leaves stomach, half-life (h)", c.needs.fluidHalfLifeH, 0.1f, 4.f, "%.1f");
        SlF("Share of fluid that becomes urine", c.needs.urineShare, 0.2f, 1.f, "%.2f");
        SlF("Urine made without drinking (ml/h)", c.needs.basalMlPerH, 0.f, 60.f, "%.0f");
        SlF("Urine production while asleep", c.needs.sleepFactor, 0.1f, 1.f, "%.2f");
        SlF("Extra urine while drunk", c.needs.diureticBoost, 0.f, 2.f, "%.2f");
        SlF("Hours from eating to the bowel", c.needs.transitHours, 4.f, 48.f, "%.0f");
        SlF("Bowel fill per meal portion", c.needs.bowelPerPortion, 0.05f, 0.8f, "%.2f");
        SeparatorText("When you feel it");
        SlF("Noticeable at", c.urge.aware, 0.1f, 0.8f, "%.2f");
        SlF("Urgent at", c.urge.urgent, 0.3f, 0.95f, "%.2f");
        SlF("Desperate at", c.urge.desperate, 0.5f, 0.99f, "%.2f");
        SlI("Reminder pause (game minutes)", c.notifyCooldownMin, 5, 600);
        Chk("Show on-screen messages", c.notifications);
        SeparatorText("Going to the toilet");
        Cmb("When desperate", c.autoRelief, "Only remind me\0Go by myself when it is safe\0");
        SlF("Pee takes (seconds)", c.reliefSecBladder, 3.f, 60.f, "%.0f");
        SlF("Bowel takes (seconds)", c.reliefSecBowel, 3.f, 90.f, "%.0f");
        Cmb("Pee pose", c.bladderPose, "By sex (men stand, women squat)\0Standing\0Standing, one hand\0Stooping\0Squatting\0Kneeling\0");
        Cmb("Bowel pose", c.bowelPose, "Squatting\0Kneeling\0Stooping\0Nature Calls pose\0");
        Chk("Allow in towns and inns", c.allowInTowns, "Off: the character waits until he is outside");
        SlF("Accident after (hours at the limit)", c.accidentHours, 0.f, 12.f, "%.1f", "0 = never. Otherwise the character has an accident after this many game hours at 100%");
        SeparatorText("Animation");
        TextWrapped("The realistic poses come from Alive Peeing and Nature Calls (keep Alive Peeing animations installed). If they are missing the character crouches with the game's own animation instead.");
        Cmb("Animation source", c.animSource, "Built-in crouch, then original mods\0Original mods, then built-in crouch\0Original mods only\0Built-in crouch only\0No animation\0");
        Chk("Black screen while relieving", c.blackScreen, "Fades out, hides everything, fades back in");
        SlF("Fade time (seconds)", c.fadeSeconds, 0.2f, 3.f, "%.1f");
        Chk("Switch to third person for the animation", c.forceThirdPerson);
    }

    void __stdcall PageIntake() {
        auto& c = g_cfg;
        TextWrapped("What a drink or meal adds to the body. Last Seed's item lists decide what is water, ale, wine and so on; the size of a portion is set here.");
        SeparatorText("Daily targets (information)");
        SlF("Fluid per day (ml)", c.dailyFluidGoalMl, 1000.f, 5000.f, "%.0f");
        SlF("Meals per day", c.dailyFoodGoal, 1.f, 6.f, "%.1f");
        SeparatorText("Drinks (ml per bottle or flagon)");
        SlF("Water", c.intake.waterMl, 50.f, 1000.f, "%.0f");
        SlF("Tea, juice, broth", c.intake.nonAlcMl, 50.f, 1000.f, "%.0f");
        SlF("Milk", c.intake.milkMl, 50.f, 1000.f, "%.0f");
        SlF("Ale, mead", c.intake.aleMl, 50.f, 1000.f, "%.0f");
        SlF("Wine", c.intake.wineMl, 50.f, 750.f, "%.0f");
        SlF("Spirits", c.intake.spiritMl, 10.f, 300.f, "%.0f");
        SlF("Skooma", c.intake.skoomaMl, 10.f, 300.f, "%.0f");
        SlF("Other drinks", c.intake.otherMl, 50.f, 1000.f, "%.0f");
        if (Chk("Count potions as a drink", c.countPotions)) c.intake.potionMl = c.countPotions ? c.potionMl : 0.f;
        if (c.countPotions && SlF("Potion size (ml)", c.potionMl, 5.f, 300.f, "%.0f")) c.intake.potionMl = c.potionMl;
        SeparatorText("Food (portion size)");
        SlF("Water inside one portion (ml)", c.intake.foodMoistureMl, 0.f, 400.f, "%.0f");
        SlF("Minor food", c.intake.tierMinor, 0.05f, 2.f);
        SlF("Medium food", c.intake.tierMedium, 0.05f, 2.f);
        SlF("Major food", c.intake.tierMajor, 0.05f, 2.f);
        SlF("Superior food", c.intake.tierSuperior, 0.05f, 3.f);
        SlF("Massive food", c.intake.tierMassive, 0.05f, 4.f);
        SeparatorText("Last Seed link");
        Chk("Thirst lowers urine production", c.useLastSeedHydration, "A dehydrated body makes less, darker urine");
        SlF("Highest Last Seed level", c.lastSeedLevelMax, 1.f, 10.f, "%.0f", "The top value of Last Seed's hunger and thirst levels");
        SlF("Urine lost per thirst level", c.thirstUrineLoss, 0.f, 0.3f);
        SeparatorText("Latest items");
        for (auto& e : RecentIntake())
            TextColored(kDim, "%s   +%.0f ml  %.2f meals", e.name.c_str(), e.fluidMl, e.portions);
    }

    void StyleTrackPage(TrackId t) {
        const int i = (int)t;
        auto& tc = i == 0 ? g_cfg.hair : g_cfg.beard;
        const char* nm = i == 0 ? "hair" : "beard";
        SeparatorText(i == 0 ? "Hair growth" : "Beard growth");
        Chk("Grow over time", tc.enabled);
        TextColored(kDim, "Wearing now: %s      Race: %s", CurrentStyleName(t).c_str(), CatalogRaceName().c_str());
        SlF("Game days per style", tc.days, 0.5f, 60.f, "%.1f", "Time to go from one style in the list to the next");
        SlF("Growth speed (%)", tc.speedPct, 10.f, 1000.f, "%.0f");
        SlI("Styles removed by one trim", tc.trimSteps, 1, 17);
        SlI("Start with style no.", tc.startStage, 1, kMaxStages, "%d", "Which style of the list is worn when you press Start");
        Chk("Needs scissors or shears to trim", tc.requireTool);
        Chk("Hunger and stress slow growth", tc.bodyFactor);
        Chk("Message when it grows", tc.notify);
        if (CollapsingHeader("Different time for each step")) {
            for (int s = 0; s < kMaxStages - 1; ++s) {
                PushID(s);
                char lab[48];
                std::snprintf(lab, sizeof lab, "Style %d to %d (0 = default)", s + 1, s + 2);
                SlF(lab, tc.perStage[s], 0.f, 60.f, "%.1f");
                PopID();
            }
        }

        SeparatorText("Your styles, shortest first");
        TextWrapped("Up to 18 styles. Slot 1 is the look right after a trim or shave; the last filled slot is the longest. "
                    "Pick from the list, or press 'Wearing' to store the style you have on right now.");
        auto& slots = g_st.slots[i];
        int pick = g_pickSlot[i];
        for (int s = 0; s < kMaxStages; ++s) {
            PushID(s);
            const bool filled = !slots[s].empty();
            if (filled) {
                Text("%2d  %s", s + 1, slots[s].name.c_str());
                SameLine(300.f); TextColored(kDim, "%s", slots[s].plugin.c_str());
            } else {
                TextColored(kDim, "%2d  (empty)", s + 1);
            }
            if (s == 0 || filled || (s > 0 && !slots[s - 1].empty())) {
                SameLine(520.f);
                if (SmallButton(pick == s ? "Choosing..." : "Choose")) g_pickSlot[i] = (pick == s ? -1 : s);
                SameLine(); if (SmallButton("Wearing")) Do([t, s] { RecordCurrent(t, s); });
            }
            if (filled) {
                SameLine(); if (SmallButton("Show")) Do([t, s] { PreviewSlot(t, s); });
                SameLine(); if (SmallButton("Up")) Do([t, s] { MoveSlot(t, s, -1); });
                SameLine(); if (SmallButton("Down")) Do([t, s] { MoveSlot(t, s, +1); });
                SameLine(); if (SmallButton("Remove")) { Do([t, s] { ClearSlot(t, s); }); if (g_pickSlot[i] == s) g_pickSlot[i] = -1; }
            }
            PopID();
        }
        if (Button("Back to the style I should wear")) Do([t] { ApplyStage(t, true); });
        if (g_st.externalChange[i]) { SameLine(); TextColored(kWarn, "A style outside the list is worn, growth is paused."); }

        pick = g_pickSlot[i];
        if (pick >= 0) {
            SeparatorText("Pick a style");
            Text("For slot %d. Type to search (name, plugin: e.g. 'Beards of Power').", pick + 1);
            SetNextItemWidth(320.f);
            InputText("##filter", g_filter[i], sizeof g_filter[i]);
            std::string f = g_filter[i];
            for (auto& ch : f) ch = (char)std::tolower((unsigned char)ch);
            if (BeginChild("##list", ImVec2(0.f, 320.f), ImGuiChildFlags_Border)) {
                std::lock_guard<std::mutex> lk(CatalogMutex());
                int shown = 0, total = 0;
                for (auto& o : Catalog(t)) {
                    if (!f.empty() && o.lower.find(f) == std::string::npos) continue;
                    ++total;
                    if (shown >= 250) continue;
                    ++shown;
                    PushID((int)(&o - Catalog(t).data()));
                    char lab[256];
                    std::snprintf(lab, sizeof lab, "%s   [%s]", o.name.c_str(), o.plugin.c_str());
                    if (Selectable(lab)) {
                        StyleOpt copy = o;
                        const int sl = pick;
                        Do([t, sl, copy] { SetSlot(t, sl, copy); PreviewSlot(t, sl); });
                        g_pickSlot[i] = -1;
                    }
                    PopID();
                }
                if (total > shown) TextColored(kDim, "%d more - type to narrow the list", total - shown);
                if (total == 0) TextColored(kDim, "Nothing found for %s.", CatalogRaceName().c_str());
            }
            EndChild();
        }
        if (i == 1) {
            SeparatorText("Shave");
            if (g_st.shaved.empty()) TextColored(kDim, "Shave goes back to slot 1.");
            else Text("Shave uses: %s", g_st.shaved.name.c_str());
            if (SmallButton("Use the style I wear now for shaving")) Do([] {
                auto* hp = Player() ? Player()->GetActorBase()->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair) : nullptr;
                if (hp && hp->GetFile(0)) {
                    g_st.shaved.plugin = std::string(hp->GetFile(0)->GetFilename());
                    g_st.shaved.local = hp->GetLocalFormID();
                    g_st.shaved.name = hp->GetName() ? hp->GetName() : "";
                    g_st.shaved.hp = hp;
                }
            });
            SameLine(); if (SmallButton("Forget")) Do([] { g_st.shaved = StyleSlot{}; });
        }
        (void)nm;
    }
    void __stdcall PageHair() { StyleTrackPage(TrackId::Hair); }
    void __stdcall PageBeard() { StyleTrackPage(TrackId::Beard); }

    void __stdcall PageHygiene() {
        auto& c = g_cfg;
        SeparatorText("Teeth");
        Chk("Teeth need brushing", c.oralEnabled);
        Chk("Dirty teeth cause stress", c.oralStress);
        SlF("Hours until teeth are dirty", c.needs.oralHours, 6.f, 168.f, "%.0f");
        TextColored(kDim, "Plaque now: %.0f%%   Last brushed: %.1f days ago", g_st.needs.oral * 100.f, std::max(0.f, DaysNow() - g_st.lastBrushDays));
        if (Button("Brush teeth now")) Do([] { BrushTeeth(); });
        SeparatorText("Grooming tools");
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s", c.toolWords.c_str());
        SetNextItemWidth(360.f);
        if (InputText("Tool words (comma separated)", buf, sizeof buf)) { c.toolWords = buf; Dirty(); }
        TextColored(kDim, "Items whose name contains one of these words count as scissors, and so does the scissors of Dynamic NPC Hairstyles.");
    }

    void SyncSpecBuffers() {
        if (g_specInit) return;
        for (int k = 0; k < (int)FormKey::COUNT; ++k) std::snprintf(g_specBuf[k], sizeof g_specBuf[k], "%s", FormSpec((FormKey)k).c_str());
        g_specInit = true;
    }
    void __stdcall PageLinks() {
        auto& c = g_cfg;
        SeparatorText("Stress and Fear");
        Chk("Link to Stress and Fear", c.stressLink);
        Chk("Unrelieved needs cause stress; relief calms", c.stressFromNeeds);
        Chk("Grooming changes stress", c.stressFromGrooming);
        SlF("Stress multiplier", c.stressScale, 0.1f, 4.f);
        TextColored(StressAvailable() ? kGood : kDim, StressAvailable() ? "Stress and Fear found, stress is %.0f" : "Not found or switched off", StressNow());
        SeparatorText("Linked records");
        TextWrapped("Records of other mods, written as Plugin.esp|HexId. Green means found in this load order. Edit only if your version of a mod uses other ids, then press Apply.");
        SyncSpecBuffers();
        for (int k = 0; k < (int)FormKey::COUNT; ++k) {
            PushID(k);
            TextColored(FormPresent((FormKey)k) ? kGood : kDim, FormPresent((FormKey)k) ? "found  " : "missing");
            SameLine(90.f);
            SetNextItemWidth(250.f);
            InputText(FormKeyLabel((FormKey)k), g_specBuf[k], sizeof g_specBuf[k]);
            PopID();
        }
        if (Button("Apply and save")) {
            for (int k = 0; k < (int)FormKey::COUNT; ++k) SetFormSpec((FormKey)k, g_specBuf[k]);
            Dirty();
            Do([] { ResolveAllForms(); });
        }
        SameLine();
        if (Button("Reload settings file")) Do([] { ReloadFromMenu(); g_specInit = false; });
    }

    void __stdcall PageHud() {
        auto& c = g_cfg;
        SeparatorText("Bars on screen");
        Chk("Bladder", c.hudBladder); SameLine(); Chk("Bowel", c.hudBowel); SameLine(); Chk("Teeth", c.hudTeeth);
        Chk("Hair", c.hudHair); SameLine(); Chk("Beard", c.hudBeard);
        Chk("Show only when something is needed", c.hudOnlyWhenNeeded);
        SlF("Position X", c.hudX, 0.f, 1800.f, "%.0f");
        SlF("Position Y", c.hudY, 0.f, 1000.f, "%.0f");
        SlF("Size", c.hudScale, 0.6f, 2.5f, "%.2f");
        SlF("Opacity", c.hudAlpha, 0.2f, 1.f, "%.2f");
        SeparatorText("Hotkeys (DirectInput scan codes, 0 = off)");
        TextColored(kDim, "F5=63  F6=64  F7=65  F8=66  F9=67  F10=68  F11=87  F12=88  (letters: G=34 H=35 J=36 K=37 L=38)");
        SlI("Pee", c.keyBladder, 0, 255);
        SlI("Bowel", c.keyBowel, 0, 255);
        SlI("Brush teeth", c.keyBrush, 0, 255);
        SlI("Trim hair", c.keyHair, 0, 255);
        SlI("Trim beard", c.keyBeard, 0, 255);
        SlI("Show status message", c.keyStatus, 0, 255);
        SeparatorText("Other");
        Chk("Start loaded games paused", c.pauseOnLoad);
        Chk("Write extra detail to the log", c.debugLog);
        if (Button("Save settings now")) Dirty();
    }

    // ------------------------------------------------------------ HUD + black screen (drawn every frame by the framework)
    ImU32 Col(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(clamp01(a) * 255.f)); }

    void DrawBar(ImDrawList* dl, float x, float y, float w, float h, float frac, ImU32 fill, const char* label, float alpha) {
        ImDrawListManager::AddRectFilled(dl, ImVec2(x, y), ImVec2(x + w, y + h), Col(20, 20, 24, 0.7f * alpha), 3.f, 0);
        ImDrawListManager::AddRectFilled(dl, ImVec2(x, y), ImVec2(x + w * clamp01(frac), y + h), fill, 3.f, 0);
        ImDrawListManager::AddText(dl, ImVec2(x + w + 8.f, y - 1.f), Col(235, 235, 235, alpha), label);
    }

    void __stdcall HudDraw() {
        auto* dl = GetForegroundDrawList();
        auto* io = GetIO();
        if (!dl || !io) return;
        const float fade = FadeAlpha();
        if (fade > 0.01f) {
            ImDrawListManager::AddRectFilled(dl, ImVec2(0, 0), ImVec2(io->DisplaySize.x, io->DisplaySize.y), IM_COL32(0, 0, 0, (int)(clamp01(fade) * 255.f)), 0.f, 0);
        }
        if (!g_st.active || g_st.paused || fade > 0.5f) return;
        auto* pl = Player();
        if (!pl || !pl->Is3DLoaded()) return;
        auto& c = g_cfg;
        const float sc = c.hudScale, a = c.hudAlpha;
        float x = c.hudX, y = c.hudY;
        const float w = 110.f * sc, h = 9.f * sc, step = 16.f * sc;
        auto row = [&](bool on, float frac, int level, ImU32 fill, const char* label) {
            if (!on) return;
            if (c.hudOnlyWhenNeeded && level < 1) return;
            DrawBar(dl, x, y, w, h, frac, fill, label, a);
            y += step;
        };
        const float bl = bladderFrac(g_st.needs, c.needs), bw = bowelFrac(g_st.needs);
        const int lb = UrgencyLevel(bl, c.urge), lw = UrgencyLevel(bw, c.urge);
        auto warm = [&](int l) { return l >= 3 ? Col(230, 70, 60, a) : (l == 2 ? Col(235, 170, 60, a) : Col(230, 215, 90, a)); };
        row(c.hudBladder, bl, lb, warm(lb), "Bladder");
        row(c.hudBowel, bw, lw, warm(lw), "Bowel");
        const int lo = g_st.needs.oral >= 0.95f ? 3 : (g_st.needs.oral >= 0.75f ? 2 : (g_st.needs.oral >= 0.5f ? 1 : 0));
        row(c.hudTeeth && c.oralEnabled, g_st.needs.oral, lo, Col(200, 200, 215, a), "Teeth");
        for (int i = 0; i < 2; ++i) {
            if (!(i == 0 ? c.hudHair : c.hudBeard)) continue;
            auto& tr = g_st.track[i];
            if (tr.count <= 1) continue;
            const float frac = tr.atMax() ? 1.f : (tr.stage + tr.progress) / (float)(tr.count - 1);
            char lab[32];
            std::snprintf(lab, sizeof lab, "%s %d/%d", i == 0 ? "Hair" : "Beard", tr.stage + 1, tr.count);
            DrawBar(dl, x, y, w, h, frac, Col(150, 120, 90, a), lab, a);
            y += step;
        }
    }
}

bool UiAvailable() { return g_ui; }

void RegisterUI() {
    if (!SKSEMenuFramework::IsInstalled()) {
        Log("SKSE Menu Framework is not installed: no menu. Settings can still be edited in WellnessGrooming.ini");
        return;
    }
    SKSEMenuFramework::SetSection("Wellness Grooming");
    SKSEMenuFramework::AddSectionItem("Start and Status", PageStatus);
    SKSEMenuFramework::AddSectionItem("Bathroom", PageBathroom);
    SKSEMenuFramework::AddSectionItem("Food and Drink", PageIntake);
    SKSEMenuFramework::AddSectionItem("Hair", PageHair);
    SKSEMenuFramework::AddSectionItem("Beard", PageBeard);
    SKSEMenuFramework::AddSectionItem("Hygiene", PageHygiene);
    SKSEMenuFramework::AddSectionItem("Linked Mods", PageLinks);
    SKSEMenuFramework::AddSectionItem("Screen and Keys", PageHud);
    SKSEMenuFramework::AddHudElement(HudDraw);
    g_ui = true;
    Log("menu registered");
}

}  // namespace WG
