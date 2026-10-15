#include "WG.h"

namespace WG {

namespace {
    struct FormDef { const char* name; const char* label; const char* def; };
    constexpr FormDef kDefs[(int)FormKey::COUNT] = {
        {"StressTotal", "Stress and Fear: stress value", "Stress and Fear.esp|801"},
        {"StressEnabled", "Stress and Fear: enabled switch", "Stress and Fear.esp|8A5"},
        {"LsHunger", "Last Seed: hunger level", "LastSeed.esp|F7D6"},
        {"LsThirst", "Last Seed: thirst level", "LastSeed.esp|F7D7"},
        {"LsNotFood", "Last Seed: not-food list", "LastSeed.esp|13A268"},
        {"LsWater", "Last Seed: water list", "LastSeed.esp|1E6534"},
        {"LsNonAlc", "Last Seed: non-alcoholic drinks", "LastSeed.esp|C71B"},
        {"LsAlc", "Last Seed: alcohol list", "LastSeed.esp|C719"},
        {"LsAle", "Last Seed: ale list", "LastSeed.esp|108EB"},
        {"LsWine", "Last Seed: wine list", "LastSeed.esp|108ED"},
        {"LsSpirit", "Last Seed: spirits list", "LastSeed.esp|108EC"},
        {"LsMilk", "Last Seed: milk list", "LastSeed.esp|C71A"},
        {"LsSkoomaWeak", "Last Seed: weak skooma list", "LastSeed.esp|2A6C35"},
        {"LsSkoomaStrong", "Last Seed: strong skooma list", "LastSeed.esp|2A6C36"},
        {"LsFoodMinor", "Last Seed: minor food list", "LastSeed.esp|707B"},
        {"LsFoodMajor", "Last Seed: major food list", "LastSeed.esp|707C"},
        {"LsFoodSuperior", "Last Seed: superior food list", "LastSeed.esp|707D"},
        {"LsFoodMassive", "Last Seed: massive food list", "LastSeed.esp|707E"},
        {"Scissors", "Dynamic NPC Hairstyles: scissors", "Dynamic NPC Hairstyles.esp|7"},
    };
    std::string g_spec[(int)FormKey::COUNT];
    RE::TESForm* g_form[(int)FormKey::COUNT] = {};
    bool g_tried[(int)FormKey::COUNT] = {};
    bool g_ready = false;
    std::mutex g_mx;

    void EnsureDefaults() {
        for (int i = 0; i < (int)FormKey::COUNT; ++i)
            if (g_spec[i].empty()) g_spec[i] = kDefs[i].def;
    }
}

const char* FormKeyName(FormKey k) { return kDefs[(int)k].name; }
const char* FormKeyLabel(FormKey k) { return kDefs[(int)k].label; }

std::string FormSpec(FormKey k) {
    std::lock_guard l(g_mx);
    EnsureDefaults();
    return g_spec[(int)k];
}
void SetFormSpec(FormKey k, const std::string& spec) {
    std::lock_guard l(g_mx);
    EnsureDefaults();
    g_spec[(int)k] = spec;
    g_form[(int)k] = nullptr;
    g_tried[(int)k] = false;
}

bool ParseSpec(const std::string& spec, std::string& plugin, std::uint32_t& local) {
    auto bar = spec.find('|');
    if (bar == std::string::npos || bar == 0 || bar + 1 >= spec.size()) return false;
    plugin = spec.substr(0, bar);
    try { local = (std::uint32_t)std::stoul(spec.substr(bar + 1), nullptr, 16); } catch (...) { return false; }
    return true;
}

bool PluginLoaded(const char* name) {
    auto* dh = RE::TESDataHandler::GetSingleton();
    return dh && name && dh->LookupModByName(name) != nullptr;
}

RE::TESForm* LookupSpec(const std::string& plugin, std::uint32_t local) {
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh || plugin.empty()) return nullptr;
    return dh->LookupForm(local & 0x00FFFFFF, plugin);
}

RE::TESForm* GetForm(FormKey k) {
    std::lock_guard l(g_mx);
    EnsureDefaults();
    const int i = (int)k;
    if (g_form[i]) return g_form[i];
    if (g_tried[i] && !g_ready) return nullptr;
    if (g_tried[i]) return nullptr;
    if (!RE::TESDataHandler::GetSingleton()) return nullptr;
    g_tried[i] = true;
    std::string p; std::uint32_t id = 0;
    if (ParseSpec(g_spec[i], p, id)) g_form[i] = LookupSpec(p, id);
    return g_form[i];
}
bool FormPresent(FormKey k) { return GetForm(k) != nullptr; }

void ResolveAllForms() {
    {
        std::lock_guard l(g_mx);
        EnsureDefaults();
        for (int i = 0; i < (int)FormKey::COUNT; ++i) { g_form[i] = nullptr; g_tried[i] = false; }
        g_ready = true;
    }
    int found = 0;
    for (int i = 0; i < (int)FormKey::COUNT; ++i) {
        if (GetForm((FormKey)i)) ++found;
        else Log(std::string("form not found (fine if the mod is not installed): ") + FormKeyName((FormKey)i) + " = " + FormSpec((FormKey)i));
    }
    Log("resolved " + std::to_string(found) + " of " + std::to_string((int)FormKey::COUNT) + " linked records");
}

}  // namespace WG
