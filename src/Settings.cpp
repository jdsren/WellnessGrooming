#include "WG.h"

namespace WG {

Settings g_cfg;
std::atomic<bool> g_settingsDirty{false};

std::string SettingsPath() { return "Data/SKSE/Plugins/WellnessGrooming.ini"; }

namespace {
    struct Opt { const char* sec; std::string key; char type; void* ptr; const char* note; };

    std::vector<Opt>& Table() {
        static std::vector<Opt> t;
        if (!t.empty()) return t;
        auto& c = g_cfg;
        auto B = [&](const char* s, const char* k, bool& v, const char* n = "") { t.push_back({s, k, 'b', &v, n}); };
        auto I = [&](const char* s, const char* k, int& v, const char* n = "") { t.push_back({s, k, 'i', &v, n}); };
        auto F = [&](const char* s, const char* k, float& v, const char* n = "") { t.push_back({s, k, 'f', &v, n}); };
        auto S = [&](const char* s, const char* k, std::string& v, const char* n = "") { t.push_back({s, k, 's', &v, n}); };

        B("General", "Notifications", c.notifications, "Show on-screen messages");
        I("General", "NotifyCooldownMinutes", c.notifyCooldownMin, "Game minutes between two reminders of the same kind");
        B("General", "DebugLog", c.debugLog, "Write extra detail to WellnessGrooming.log");
        B("General", "PauseOnLoad", c.pauseOnLoad, "Start every loaded game paused");

        F("Needs", "BladderCapacityMl", c.needs.bladderCapMl);
        F("Needs", "FluidHalfLifeHours", c.needs.fluidHalfLifeH, "How fast drinks leave the stomach");
        F("Needs", "UrineShare", c.needs.urineShare, "Share of absorbed fluid that becomes urine");
        F("Needs", "BasalMlPerHour", c.needs.basalMlPerH, "Urine made per hour even without drinking");
        F("Needs", "SleepFactor", c.needs.sleepFactor, "Urine production while sleeping");
        F("Needs", "DiureticBoost", c.needs.diureticBoost, "Extra urine while alcohol is active");
        F("Needs", "TransitHours", c.needs.transitHours, "Hours from eating to the bowel");
        F("Needs", "BowelPerPortion", c.needs.bowelPerPortion, "Share of bowel capacity filled by one meal portion");
        F("Needs", "UrgeAware", c.urge.aware);
        F("Needs", "UrgeUrgent", c.urge.urgent);
        F("Needs", "UrgeDesperate", c.urge.desperate);
        B("Needs", "UseLastSeedHydration", c.useLastSeedHydration, "Thirst in Last Seed lowers urine production");
        F("Needs", "LastSeedLevelMax", c.lastSeedLevelMax, "Highest value of Last Seed's hunger and thirst levels");
        F("Needs", "ThirstUrineLoss", c.thirstUrineLoss, "Urine lost per thirst level");
        F("Needs", "DailyFluidGoalMl", c.dailyFluidGoalMl, "Shown as the daily target in the menu");
        F("Needs", "DailyFoodGoal", c.dailyFoodGoal, "Meal portions per day shown as the target");

        F("Intake", "WaterMl", c.intake.waterMl);
        F("Intake", "NonAlcoholicMl", c.intake.nonAlcMl);
        F("Intake", "MilkMl", c.intake.milkMl);
        F("Intake", "AleMl", c.intake.aleMl);
        F("Intake", "WineMl", c.intake.wineMl);
        F("Intake", "SpiritMl", c.intake.spiritMl);
        F("Intake", "SkoomaMl", c.intake.skoomaMl);
        F("Intake", "OtherDrinkMl", c.intake.otherMl);
        B("Intake", "CountPotions", c.countPotions);
        F("Intake", "PotionMl", c.potionMl);
        F("Intake", "FoodMoistureMl", c.intake.foodMoistureMl, "Water inside one meal portion");
        F("Intake", "PortionMinor", c.intake.tierMinor);
        F("Intake", "PortionMedium", c.intake.tierMedium);
        F("Intake", "PortionMajor", c.intake.tierMajor);
        F("Intake", "PortionSuperior", c.intake.tierSuperior);
        F("Intake", "PortionMassive", c.intake.tierMassive);

        I("Relief", "AutoRelief", c.autoRelief, "0 = only remind, 1 = go by yourself when it is safe");
        F("Relief", "BladderSeconds", c.reliefSecBladder);
        F("Relief", "BowelSeconds", c.reliefSecBowel);
        I("Relief", "AnimationSource", c.animSource, "0 built-in then original mods, 1 original then built-in, 2 original only, 3 built-in only, 4 none");
        B("Relief", "BlackScreen", c.blackScreen, "Fade to black while relieving yourself");
        F("Relief", "FadeSeconds", c.fadeSeconds);
        B("Relief", "ForceThirdPerson", c.forceThirdPerson);
        I("Relief", "BladderPose", c.bladderPose, "0 auto by sex, 1 standing, 2 standing one-handed, 3 stooping, 4 squatting, 5 kneeling");
        I("Relief", "BowelPose", c.bowelPose, "0 squatting, 1 kneeling, 2 stooping, 3 Nature Calls pose");
        B("Relief", "AllowInTowns", c.allowInTowns);
        F("Relief", "AccidentHours", c.accidentHours, "0 = never; game hours at the limit before an accident happens");

        B("Stress", "StressAndFearLink", c.stressLink);
        F("Stress", "StressScale", c.stressScale);
        B("Stress", "StressFromNeeds", c.stressFromNeeds);
        B("Stress", "StressFromGrooming", c.stressFromGrooming);

        for (int i = 0; i < 2; ++i) {
            auto& tr = i == 0 ? c.hair : c.beard;
            const char* sec = i == 0 ? "Hair" : "Beard";
            B(sec, "Enabled", tr.enabled);
            F(sec, "DaysPerStage", tr.days, "Game days from one style to the next");
            F(sec, "SpeedPercent", tr.speedPct);
            I(sec, "TrimSteps", tr.trimSteps, "Styles removed by one trim");
            B(sec, "RequireTool", tr.requireTool, "Need scissors (or similar) in the inventory");
            B(sec, "BodyFactor", tr.bodyFactor, "Hunger and stress slow growth");
            B(sec, "Notify", tr.notify);
            I(sec, "StartStage", tr.startStage);
            for (int s = 0; s < kMaxStages - 1; ++s)
                t.push_back({sec, std::string("StageDays") + std::to_string(s + 1), 'f', &tr.perStage[s], i == 0 && s == 0 ? "Days from style N to N+1, 0 = use DaysPerStage" : ""});
        }
        S("Tools", "ToolWords", c.toolWords, "Items whose name contains one of these words count as grooming tools");

        B("Hygiene", "OralCare", c.oralEnabled);
        B("Hygiene", "OralStress", c.oralStress);
        F("Hygiene", "OralHours", c.needs.oralHours, "Hours until teeth need brushing");

        B("Hud", "Bladder", c.hudBladder);
        B("Hud", "Bowel", c.hudBowel);
        B("Hud", "Hair", c.hudHair);
        B("Hud", "Beard", c.hudBeard);
        B("Hud", "Teeth", c.hudTeeth);
        B("Hud", "OnlyWhenNeeded", c.hudOnlyWhenNeeded);
        F("Hud", "X", c.hudX);
        F("Hud", "Y", c.hudY);
        F("Hud", "Scale", c.hudScale);
        F("Hud", "Alpha", c.hudAlpha);

        I("Keys", "Bladder", c.keyBladder, "DirectInput scan code, 0 = off (F5=63 F6=64 F7=65 F8=66 F9=67 F10=68)");
        I("Keys", "Bowel", c.keyBowel);
        I("Keys", "Brush", c.keyBrush);
        I("Keys", "Hair", c.keyHair);
        I("Keys", "Beard", c.keyBeard);
        I("Keys", "Status", c.keyStatus);
        return t;
    }

    std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }
    bool IEq(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return true;
    }
}

void LoadSettings() {
    std::ifstream f(SettingsPath());
    if (!f) { Log("no settings file, using defaults"); return; }
    std::string line, sec;
    auto& tab = Table();
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { sec = Trim(line.substr(1, line.find(']') - 1)); continue; }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
        auto sc = val.find(" ;");
        if (sc != std::string::npos) val = Trim(val.substr(0, sc));
        if (IEq(sec, "Forms")) {
            for (int i = 0; i < (int)FormKey::COUNT; ++i)
                if (IEq(key, FormKeyName((FormKey)i))) SetFormSpec((FormKey)i, val);
            continue;
        }
        for (auto& o : tab) {
            if (!IEq(sec, o.sec) || !IEq(key, o.key)) continue;
            try {
                switch (o.type) {
                case 'b': *(bool*)o.ptr = (val == "1" || IEq(val, "true") || IEq(val, "yes") || IEq(val, "on")); break;
                case 'i': *(int*)o.ptr = std::stoi(val); break;
                case 'f': *(float*)o.ptr = std::stof(val); break;
                case 's': *(std::string*)o.ptr = val; break;
                }
            } catch (...) { Log("bad value for " + key + " = " + val); }
            break;
        }
    }
    // sanity
    auto& c = g_cfg;
    c.needs.bladderCapMl = clampf(c.needs.bladderCapMl, 150.f, 1500.f);
    c.needs.fluidHalfLifeH = clampf(c.needs.fluidHalfLifeH, 0.1f, 6.f);
    c.needs.transitHours = clampf(c.needs.transitHours, 2.f, 72.f);
    c.needs.urineShare = clampf(c.needs.urineShare, 0.1f, 1.f);
    c.needs.bowelPerPortion = clampf(c.needs.bowelPerPortion, 0.05f, 1.f);
    c.needs.oralHours = clampf(c.needs.oralHours, 4.f, 240.f);
    c.urge.aware = clampf(c.urge.aware, 0.05f, 0.95f);
    c.urge.urgent = clampf(c.urge.urgent, c.urge.aware + 0.02f, 0.98f);
    c.urge.desperate = clampf(c.urge.desperate, c.urge.urgent + 0.02f, 0.995f);
    c.intake.potionMl = c.countPotions ? c.potionMl : 0.f;
    for (auto* t : {&c.hair, &c.beard}) { t->days = clampf(t->days, 0.1f, 200.f); t->trimSteps = std::clamp(t->trimSteps, 1, 17); t->startStage = std::clamp(t->startStage, 1, kMaxStages); t->speedPct = clampf(t->speedPct, 5.f, 2000.f); }
}

void SaveSettings() {
    std::error_code ec;
    std::filesystem::create_directories("Data/SKSE/Plugins", ec);
    std::ofstream f(SettingsPath(), std::ios::trunc);
    if (!f) { Log("could not write settings"); return; }
    f << "; WellnessGrooming settings. Everything here can also be changed in the in-game menu (SKSE Menu Framework).\n";
    std::string sec;
    for (auto& o : Table()) {
        if (sec != o.sec) { sec = o.sec; f << "\n[" << sec << "]\n"; }
        if (o.note && o.note[0]) f << "; " << o.note << "\n";
        f << o.key << "=";
        switch (o.type) {
        case 'b': f << (*(bool*)o.ptr ? "true" : "false"); break;
        case 'i': f << *(int*)o.ptr; break;
        case 'f': { char b[32]; std::snprintf(b, sizeof b, "%g", *(float*)o.ptr); f << b; break; }
        case 's': f << *(std::string*)o.ptr; break;
        }
        f << "\n";
    }
    f << "\n[Forms]\n; Records of other mods, as 'Plugin.esp|LocalHexId'. Change them if your build of the mod uses other ids.\n";
    for (int i = 0; i < (int)FormKey::COUNT; ++i) f << FormKeyName((FormKey)i) << "=" << FormSpec((FormKey)i) << "\n";
    g_settingsDirty = false;
}

}  // namespace WG
