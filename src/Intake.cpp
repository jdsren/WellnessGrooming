#include "WG.h"

namespace WG {

namespace {
    std::deque<IntakeLogEntry> g_recent;

    bool InList(FormKey k, RE::TESForm* f) {
        auto* l = skyrim_cast<RE::BGSListForm*>(GetForm(k));
        return l && f && l->HasForm(f);
    }
    float GlobalValue(FormKey k) {
        auto* g = skyrim_cast<RE::TESGlobal*>(GetForm(k));
        return g ? g->value : 0.f;
    }
    std::string Lower(const char* s) {
        std::string r = s ? s : "";
        for (auto& c : r) c = (char)std::tolower((unsigned char)c);
        return r;
    }
    // matches at the start of a word, so "ale" does not hit "pale" or "scale"
    bool Has(const std::string& s, std::initializer_list<const char*> words) {
        for (auto* w : words) {
            size_t pos = 0;
            const std::string needle = w;
            while ((pos = s.find(needle, pos)) != std::string::npos) {
                if (pos == 0 || !std::isalpha((unsigned char)s[pos - 1])) return true;
                ++pos;
            }
        }
        return false;
    }
    bool HasKw(RE::AlchemyItem* it, const char* edid) {
        return it->HasKeywordString(edid);
    }

    struct EquipSink : RE::BSTEventSink<RE::TESEquipEvent> {
        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* e, RE::BSTEventSource<RE::TESEquipEvent>*) override {
            if (!e || !e->equipped || !e->actor || !e->actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
            auto* it = RE::TESForm::LookupByID<RE::AlchemyItem>(e->baseObject);
            if (!it) return RE::BSEventNotifyControl::kContinue;
            Post([it]() { HandleConsumed(it); });
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    EquipSink g_sink;
}

float ThirstLevel01() {
    const float m = std::max(1.f, g_cfg.lastSeedLevelMax);
    return clamp01(GlobalValue(FormKey::LsThirst) / m);
}
float HungerLevel01() {
    const float m = std::max(1.f, g_cfg.lastSeedLevelMax);
    return clamp01(GlobalValue(FormKey::LsHunger) / m);
}
float HydrationFactor() {
    if (!g_cfg.useLastSeedHydration || !FormPresent(FormKey::LsThirst)) return 1.f;
    const float level = ThirstLevel01() * std::max(1.f, g_cfg.lastSeedLevelMax);
    return clampf(1.f - g_cfg.thirstUrineLoss * level, 0.4f, 1.f);
}

const std::vector<IntakeLogEntry>& RecentIntake() {
    static std::vector<IntakeLogEntry> v;
    v.assign(g_recent.begin(), g_recent.end());
    return v;
}

ItemTraits Classify(RE::AlchemyItem* it, bool& ingestible) {
    ItemTraits t;
    ingestible = false;
    if (!it || it->IsPoison()) return t;
    if (InList(FormKey::LsNotFood, it)) return t;
    const std::string n = Lower(it->GetName());
    const bool foodFlag = it->IsFood() || HasKw(it, "VendorItemFood") || HasKw(it, "VendorItemFoodRaw");

    // drinks: Last Seed lists first, then names
    if (InList(FormKey::LsWater, it)) t.drink = DrinkKind::Water;
    else if (InList(FormKey::LsMilk, it)) t.drink = DrinkKind::Milk;
    else if (InList(FormKey::LsAle, it)) t.drink = DrinkKind::Ale;
    else if (InList(FormKey::LsWine, it)) t.drink = DrinkKind::Wine;
    else if (InList(FormKey::LsSpirit, it)) t.drink = DrinkKind::Spirit;
    else if (InList(FormKey::LsSkoomaWeak, it) || InList(FormKey::LsSkoomaStrong, it)) t.drink = DrinkKind::Skooma;
    else if (InList(FormKey::LsNonAlc, it)) t.drink = DrinkKind::NonAlcoholic;
    else if (InList(FormKey::LsAlc, it)) t.drink = DrinkKind::Ale;
    else if (foodFlag) {
        if (Has(n, {"skooma"})) t.drink = DrinkKind::Skooma;
        else if (Has(n, {"water"}) && !Has(n, {"waterm"})) t.drink = DrinkKind::Water;
        else if (Has(n, {"milk"})) t.drink = DrinkKind::Milk;
        else if (Has(n, {"mead", "ale", "beer", "stout", "lager", "cider", "flagon", "tankard"}) && !Has(n, {"pale", "scale", "male", "kale"})) t.drink = DrinkKind::Ale;
        else if (Has(n, {"wine", "sujamma", "shein", "brandy", "port"}) && !Has(n, {"portion", "report"})) t.drink = (Has(n, {"brandy"}) ? DrinkKind::Spirit : DrinkKind::Wine);
        else if (Has(n, {"whisky", "whiskey", "rum", "vodka", "liquor", "spirit", "flin", "matze", "greef"})) t.drink = DrinkKind::Spirit;
        else if (Has(n, {"tea", "juice", "coffee", "cocoa", "nectar", "cordial", "lemonade", "broth"})) t.drink = DrinkKind::NonAlcoholic;
    }
    if (t.drink != DrinkKind::None) { ingestible = true; return t; }

    // food
    FoodTier tier = FoodTier::None;
    if (InList(FormKey::LsFoodMassive, it)) tier = FoodTier::Massive;
    else if (InList(FormKey::LsFoodSuperior, it)) tier = FoodTier::Superior;
    else if (InList(FormKey::LsFoodMajor, it)) tier = FoodTier::Major;
    else if (InList(FormKey::LsFoodMinor, it)) tier = FoodTier::Minor;
    else if (foodFlag) {
        if (HasKw(it, "VendorItemFoodRaw")) tier = FoodTier::Minor;
        else if (Has(n, {"stew", "pie", "roast", "feast", "dinner", "pudding", "steak", "casserole", "platter"})) tier = FoodTier::Major;
        else if (Has(n, {"soup", "bread", "cheese", "sweetroll", "cake", "tart", "pastry", "dumpling", "sausage", "chop", "leg", "potato", "cabbage"})) tier = FoodTier::Medium;
        else tier = FoodTier::Minor;
    }
    if (tier != FoodTier::None) {
        t.tier = tier;
        t.fiber = 1.f;
        if (Has(n, {"meat", "steak", "chop", "sausage", "venison", "beef", "pork", "mutton", "cheese", "egg", "fish", "salmon", "boar", "leg", "chicken", "horker", "mammoth"})) t.fiber = 0.7f;
        else if (Has(n, {"bread", "cabbage", "potato", "carrot", "leek", "gourd", "apple", "vegetable", "salad", "wheat", "tomato", "turnip", "squash"})) t.fiber = 1.3f;
        if (Has(n, {"sweetroll", "cake", "tart", "pastry", "honey", "jazbay", "sugar", "pudding", "candy", "snowberr", "cookie", "dessert", "pie"})) t.sugar = 0.8f;
        ingestible = true;
        return t;
    }

    // plain potions (health, magicka, ...): only if the player wants them counted
    if (g_cfg.countPotions) { t.drink = DrinkKind::Potion; ingestible = true; }
    return t;
}

void HandleConsumed(RE::AlchemyItem* item) {
    if (!g_st.active || g_st.paused || !item) return;
    bool ok = false;
    const auto traits = Classify(item, ok);
    if (!ok) return;
    const auto r = ComputeIntake(traits, g_cfg.intake);
    if (!r.counted) return;
    AddFluid(g_st.needs, r.fluidMl, r.diureticH);
    if (r.portions > 0.f) AddFood(g_st.needs, g_cfg.needs, r.portions, r.fiber, r.sugar);
    RollDay(g_st.day, (int)DaysNow());
    g_st.day.fluidMl += r.fluidMl;
    g_st.day.portions += r.portions;
    g_recent.push_back({item->GetName() ? item->GetName() : "?", r.fluidMl, r.portions, DaysNow()});
    while (g_recent.size() > 12) g_recent.pop_front();
    if (g_cfg.debugLog) Log(std::format("consumed {}: {:.0f} ml, {:.2f} portions", item->GetName(), r.fluidMl, r.portions));
}

void RegisterIntake() {
    if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) src->AddEventSink<RE::TESEquipEvent>(&g_sink);
}

}  // namespace WG
