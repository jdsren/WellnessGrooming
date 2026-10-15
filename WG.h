// WellnessGrooming - shared declarations.
#pragma once
#include "PCH.h"
#include "Model.h"

namespace WG {
using namespace wgm;

// ------------------------------------------------------------------ settings (global, INI + menu)
struct Settings {
    // general
    bool notifications = true;
    int notifyCooldownMin = 60;           // game minutes between two reminders of the same kind
    bool debugLog = false;
    bool pauseOnLoad = false;

    // needs model
    NeedsParams needs;
    Thresholds urge;
    IntakeParams intake;
    bool countPotions = false;
    float potionMl = 40.f;
    float dailyFluidGoalMl = 2500.f;      // information only: shown as the daily target in the menu
    float dailyFoodGoal = 3.0f;
    bool useLastSeedHydration = true;     // dehydration (Last Seed thirst) lowers urine production
    float lastSeedLevelMax = 5.f;         // highest value of Last Seed's hunger/thirst level globals
    float thirstUrineLoss = 0.10f;        // urine production lost per thirst level

    // relief
    int autoRelief = 1;                   // 0 = only remind, 1 = go by yourself when it is safe
    float reliefSecBladder = 12.f;
    float reliefSecBowel = 20.f;
    int animSource = 1;                   // 0 built-in crouch then original mods, 1 original mods then built-in, 2 original only, 3 built-in only, 4 none
    bool blackScreen = false;
    float fadeSeconds = 0.8f;
    bool forceThirdPerson = true;
    int bladderPose = 0;                  // 0 auto by sex, 1 standing, 2 standing (1h), 3 stooping, 4 squatting, 5 kneeling
    int bowelPose = 0;                    // 0 squatting, 1 kneeling, 2 stooping, 3 Nature Calls pose
    bool allowInTowns = true;
    float accidentHours = 0.f;            // 0 = never; otherwise game hours of "cannot hold it" before an accident

    // stress & fear
    bool stressLink = true;
    float stressScale = 1.0f;             // multiplier for every stress change this mod makes
    bool stressFromNeeds = true;
    bool stressFromGrooming = true;

    // grooming
    struct Track {
        bool enabled = true;
        float days = 8.f;                 // game days per stage
        std::array<float, kMaxStages> perStage{};
        float speedPct = 100.f;
        int trimSteps = 2;
        bool requireTool = true;
        bool bodyFactor = true;           // starvation and stress slow growth
        bool notify = true;
        int startStage = 1;
    } hair, beard;
    std::string toolWords = "scissors,shears,razor,clippers";
    bool oralEnabled = true;
    bool oralStress = true;

    // hud
    bool hudBladder = true, hudBowel = true, hudHair = false, hudBeard = false, hudTeeth = false;
    bool hudOnlyWhenNeeded = true;
    float hudX = 24.f, hudY = 260.f, hudScale = 1.f, hudAlpha = 0.85f;

    // hotkeys (DirectInput scan codes, 0 = off)
    int keyBladder = 0, keyBowel = 0, keyBrush = 0, keyHair = 0, keyBeard = 0, keyStatus = 0;

    Settings() {
        hair.days = 8.f; beard.days = 3.5f; beard.trimSteps = 2;
        intake.potionMl = 0;
    }
};

extern Settings g_cfg;
void LoadSettings();
void SaveSettings();
extern std::atomic<bool> g_settingsDirty;
std::string SettingsPath();

// ------------------------------------------------------------------ forms of other mods (resolved by plugin|local id)
enum class FormKey {
    StressTotal, StressEnabled,
    LsHunger, LsThirst, LsNotFood,
    LsWater, LsNonAlc, LsAlc, LsAle, LsWine, LsSpirit, LsMilk, LsSkoomaWeak, LsSkoomaStrong,
    LsFoodMinor, LsFoodMajor, LsFoodSuperior, LsFoodMassive,
    Scissors,
    COUNT
};
RE::TESForm* GetForm(FormKey k);
bool FormPresent(FormKey k);
std::string FormSpec(FormKey k);              // current "Plugin.esp|HEX"
void SetFormSpec(FormKey k, const std::string& spec);
const char* FormKeyName(FormKey k);
const char* FormKeyLabel(FormKey k);
void ResolveAllForms();                       // after data load
RE::TESForm* LookupSpec(const std::string& plugin, std::uint32_t local);
bool ParseSpec(const std::string& spec, std::string& plugin, std::uint32_t& local);
bool PluginLoaded(const char* name);

// ------------------------------------------------------------------ styles
struct StyleOpt {
    RE::BGSHeadPart* hp = nullptr;
    std::uint32_t formID = 0;
    std::string name;     // display name
    std::string plugin;   // source plugin
    std::string edid;
    std::string lower;    // lower-cased search text
    bool male = true, female = true;
};
struct StyleSlot {
    std::string plugin;
    std::uint32_t local = 0;
    std::string name;
    RE::BGSHeadPart* hp = nullptr;
    bool empty() const { return plugin.empty(); }
};
enum class TrackId { Hair = 0, Beard = 1 };

// ------------------------------------------------------------------ per-save state
struct SaveState {
    bool active = false;
    bool paused = false;
    float lastDays = -1.f;
    NeedsState needs;
    DayStats day;
    GrowthTrack track[2];
    std::array<StyleSlot, kMaxStages> slots[2];
    StyleSlot shaved;                    // optional "clean shaven" look
    float lastNotifyDays[8] = {-99, -99, -99, -99, -99, -99, -99, -99};
    float lastVoidDays = 0, lastBowelDays = 0, lastBrushDays = 0, lastCutDays[2] = {0, 0};
    float fullHours = 0;                 // hours spent at "cannot hold it"
    bool externalChange[2] = {false, false};
    int lastUrgency[3] = {0, 0, 0};
};
extern SaveState g_st;
void ResetState();

// ------------------------------------------------------------------ runtime helpers (Core.cpp)
RE::PlayerCharacter* Player();
void Post(std::function<void()> fn);                 // run on the game's main thread
float DaysNow();
bool GameBusy();                                     // menus / loading
void Notify(const std::string& msg);
void SetPlayerControls(bool enable);                 // movement / combat / activate etc.
void Log(const std::string& msg);
bool PlayerIsFemale();

void Install();                                      // kDataLoaded
void OnLoaded(bool newGame);
void SaveTo(SKSE::SerializationInterface* a);
void LoadFrom(SKSE::SerializationInterface* a);
void Revert();
void StartTickThread();

// control
void StartMod();
void StopMod();
void PauseMod(bool pause);
void ResetNeeds();
void ReloadFromMenu();

// ------------------------------------------------------------------ intake (Intake.cpp)
struct IntakeLogEntry { std::string name; float fluidMl; float portions; float days; };
void RegisterIntake();
void HandleConsumed(RE::AlchemyItem* item);
ItemTraits Classify(RE::AlchemyItem* item, bool& isIngestible);
const std::vector<IntakeLogEntry>& RecentIntake();
float HydrationFactor();                             // Last Seed thirst -> 0.4..1
float HungerLevel01();
float ThirstLevel01();

// ------------------------------------------------------------------ relief (Relief.cpp)
enum class ReliefKind { Bladder, Bowel };
bool CanRelieveNow(std::string* why = nullptr);
bool StartRelief(ReliefKind k, bool automatic);
void CancelRelief(const char* why);
bool ReliefActive();
void UpdateRelief(float dtSec);                      // main thread, every tick
float FadeAlpha();                                   // 0..1 black screen overlay
std::string ReliefStatus();
bool OriginalAnimationsSeen();                       // did an original-mod event get accepted so far
bool BuiltInAnimationSeen();
void OnNeedsTick(float hours);                       // urgency reminders, auto relief, stress

// ------------------------------------------------------------------ grooming (Grooming.cpp)
void BuildCatalog();                                 // all hair / beard head parts
void RefreshCatalogFilter();                         // for the player's race and sex
std::mutex& CatalogMutex();                          // hold while reading Catalog() from the menu thread
const std::vector<StyleOpt>& Catalog(TrackId t);     // already filtered for race and sex
std::string CatalogRaceName();
void ResolveSlots();                                 // from plugin|local to pointers (after load)
bool SetSlot(TrackId t, int idx, const StyleOpt& opt);
void ClearSlot(TrackId t, int idx);
void MoveSlot(TrackId t, int idx, int dir);
void RecordCurrent(TrackId t, int idx);
int SlotCount(TrackId t);                            // consecutive filled slots from 1
void PreviewSlot(TrackId t, int idx);
void ApplyStage(TrackId t, bool force);
void AdvanceGroomingDays(float days);
bool Trim(TrackId t, int steps, bool toStage = false, int stage = 0);
bool Shave();
bool HasTool(TrackId t);
std::string CurrentStyleName(TrackId t);
void SyncExternalChanges();
void BeginStartApply();                              // used by StartMod
void TickGrooming();

// ------------------------------------------------------------------ misc actions
void BrushTeeth();
void AddStress(float delta, const char* reason);
float StressNow();
bool StressAvailable();

// ------------------------------------------------------------------ UI (UI.cpp)
void RegisterUI();
bool UiAvailable();

}  // namespace WG
