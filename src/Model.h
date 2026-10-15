// Pure logic of WellnessGrooming: no game types in here, so it can be unit-tested on any machine.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace wgm {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --------------------------------------------------------------------------------------------
// Body needs: fluid -> bladder, food -> bowel, plaque -> oral care
// --------------------------------------------------------------------------------------------
struct NeedsParams {
    float bladderCapMl = 450.f;      // volume at which the bladder is 100 % full
    float fluidHalfLifeH = 0.6f;     // half-life of fluid waiting in the stomach (game hours)
    float urineShare = 0.60f;        // share of absorbed fluid that ends up as urine (rest: sweat, breath, stool)
    float basalMlPerH = 20.f;        // obligatory urine production per hour (kidneys never stop)
    float sleepFactor = 0.50f;       // urine production while sleeping
    float diureticBoost = 0.60f;     // extra urine production while alcohol / caffeine is active
    float transitHours = 12.f;       // mean time from eating to the colon
    float bowelPerPortion = 0.30f;   // share of bowel capacity filled by one average food portion
    float oralHours = 36.f;          // hours until teeth are "dirty" (1.0) without brushing
};

struct NeedsState {
    float stomachFluid = 0.f;  // ml still to be absorbed
    float bladder = 0.f;       // ml
    float transit0 = 0.f;      // food on its way (bowel capacity units), 3-stage delay chain
    float transit1 = 0.f;
    float colon = 0.f;         // bowel fill, 1.0 = full
    float oral = 0.f;          // plaque, 0..1
    float diuresisH = 0.f;     // hours of diuretic effect left
};

struct StepContext {
    bool sleeping = false;
    float hydration = 1.f;  // 1 = normal; <1 when the body is dehydrated (concentrated, less urine)
    float exertion = 0.f;   // 0..1, sweating reduces urine production
};

inline float bladderFrac(const NeedsState& s, const NeedsParams& p) { return s.bladder / std::max(1.f, p.bladderCapMl); }
inline float bowelFrac(const NeedsState& s) { return s.colon; }

inline void StepNeeds(NeedsState& s, const NeedsParams& p, float hours, const StepContext& c) {
    if (hours <= 0.f) return;
    float left = hours;
    while (left > 0.f) {
        const float dt = std::min(left, 0.25f);
        left -= dt;
        // stomach -> blood -> bladder
        const float k = 0.69314718f / std::max(0.05f, p.fluidHalfLifeH);
        const float absorbed = s.stomachFluid * (1.f - std::exp(-k * dt));
        s.stomachFluid -= absorbed;
        float diuretic = (s.diuresisH > 0.f) ? (1.f + p.diureticBoost) : 1.f;
        float state = c.hydration * (1.f - 0.4f * clamp01(c.exertion)) * (c.sleeping ? p.sleepFactor : 1.f);
        float inflow = (absorbed * p.urineShare + p.basalMlPerH * dt) * diuretic * state;
        s.bladder = std::min(s.bladder + inflow, p.bladderCapMl * 1.25f);
        s.diuresisH = std::max(0.f, s.diuresisH - dt);
        // food: three equal delay stages, then it sits in the colon
        const float k3 = 3.f / std::max(0.5f, p.transitHours);
        const float f = 1.f - std::exp(-k3 * dt);
        const float m0 = s.transit0 * f;
        const float m1 = s.transit1 * f;
        s.transit0 -= m0;
        s.transit1 += m0 - m1;
        s.colon = std::min(s.colon + m1, 1.25f);
        // teeth
        s.oral = std::min(1.f, s.oral + dt / std::max(1.f, p.oralHours));
    }
}

inline void AddFluid(NeedsState& s, float ml, float diureticHours = 0.f) {
    s.stomachFluid += ml;
    s.diuresisH = std::max(s.diuresisH, diureticHours);
}
inline void AddFood(NeedsState& s, const NeedsParams& p, float portions, float fiber, float sugar) {
    s.transit0 += portions * p.bowelPerPortion * fiber;
    s.oral = std::min(1.f, s.oral + 0.03f * portions + 0.06f * sugar);
}

// urgency 0 = none, 1 = aware, 2 = urgent, 3 = desperate, 4 = cannot hold it
struct Thresholds { float aware = 0.40f, urgent = 0.70f, desperate = 0.90f, limit = 1.00f; };
inline int UrgencyLevel(float frac, const Thresholds& t) {
    if (frac >= t.limit) return 4;
    if (frac >= t.desperate) return 3;
    if (frac >= t.urgent) return 2;
    if (frac >= t.aware) return 1;
    return 0;
}

// --------------------------------------------------------------------------------------------
// Intake classification (what a consumed item adds)
// --------------------------------------------------------------------------------------------
enum class DrinkKind { None, Water, NonAlcoholic, Milk, Ale, Wine, Spirit, Skooma, Other, Potion };
enum class FoodTier { None, Minor, Medium, Major, Superior, Massive };

struct IntakeParams {
    float waterMl = 300, nonAlcMl = 250, milkMl = 250, aleMl = 350, wineMl = 150, spiritMl = 50, skoomaMl = 30,
          otherMl = 250, potionMl = 0;                 // potions: 0 = not counted
    float foodMoistureMl = 120;                         // water contained in one average food portion
    float tierMinor = 0.35f, tierMedium = 0.60f, tierMajor = 0.90f, tierSuperior = 1.20f, tierMassive = 1.60f;
    float diureticHoursAle = 2.5f, diureticHoursWine = 2.0f, diureticHoursSpirit = 3.0f;
};

struct ItemTraits {
    DrinkKind drink = DrinkKind::None;
    FoodTier tier = FoodTier::None;   // set when the item is food
    float fiber = 1.f;                // 0.6 meat/cheese .. 1.4 vegetables/bread
    float sugar = 0.f;                // 0..1 treats and sweets
};

struct IntakeResult { float fluidMl = 0, portions = 0, fiber = 1, sugar = 0, diureticH = 0; bool counted = false; };

inline IntakeResult ComputeIntake(const ItemTraits& t, const IntakeParams& p) {
    IntakeResult r;
    switch (t.drink) {
    case DrinkKind::Water: r.fluidMl = p.waterMl; break;
    case DrinkKind::NonAlcoholic: r.fluidMl = p.nonAlcMl; break;
    case DrinkKind::Milk: r.fluidMl = p.milkMl; break;
    case DrinkKind::Ale: r.fluidMl = p.aleMl; r.diureticH = p.diureticHoursAle; break;
    case DrinkKind::Wine: r.fluidMl = p.wineMl; r.diureticH = p.diureticHoursWine; break;
    case DrinkKind::Spirit: r.fluidMl = p.spiritMl; r.diureticH = p.diureticHoursSpirit; break;
    case DrinkKind::Skooma: r.fluidMl = p.skoomaMl; break;
    case DrinkKind::Other: r.fluidMl = p.otherMl; break;
    case DrinkKind::Potion: r.fluidMl = p.potionMl; break;
    case DrinkKind::None: break;
    }
    if (t.tier != FoodTier::None) {
        switch (t.tier) {
        case FoodTier::Minor: r.portions = p.tierMinor; break;
        case FoodTier::Medium: r.portions = p.tierMedium; break;
        case FoodTier::Major: r.portions = p.tierMajor; break;
        case FoodTier::Superior: r.portions = p.tierSuperior; break;
        case FoodTier::Massive: r.portions = p.tierMassive; break;
        default: break;
        }
        r.fluidMl += p.foodMoistureMl * r.portions;
        r.fiber = t.fiber;
        r.sugar = t.sugar;
    }
    r.counted = r.fluidMl > 0.f || r.portions > 0.f;
    return r;
}

// --------------------------------------------------------------------------------------------
// Hair / beard growth
// --------------------------------------------------------------------------------------------
constexpr int kMaxStages = 18;

struct GrowthParams {
    float defaultDays = 8.f;                    // game days from one stage to the next
    std::array<float, kMaxStages> perStage{};   // 0 = use default; index i = stage i -> i+1
    float speed = 1.f;                          // global multiplier
};

struct GrowthTrack {
    int count = 0;       // how many stages are filled in (consecutive from slot 1)
    int stage = 0;       // current stage index 0..count-1
    float progress = 0;  // 0..1 towards the next stage
    bool atMax() const { return count <= 0 || stage >= count - 1; }
};

inline float StageDays(const GrowthParams& g, int stage) {
    float d = (stage >= 0 && stage < kMaxStages) ? g.perStage[stage] : 0.f;
    return d > 0.f ? d : std::max(0.1f, g.defaultDays);
}

// Advance by `days` (already multiplied by body factors). Returns number of stages gained.
inline int AdvanceGrowth(GrowthTrack& t, const GrowthParams& g, float days) {
    if (t.count <= 1 || days <= 0.f) return 0;
    int gained = 0;
    float left = days * std::max(0.f, g.speed);
    while (left > 0.f && !t.atMax()) {
        float need = (1.f - t.progress) * StageDays(g, t.stage);
        if (left >= need) {
            left -= need;
            t.stage++;
            t.progress = 0.f;
            gained++;
        } else {
            t.progress += left / StageDays(g, t.stage);
            left = 0.f;
        }
    }
    if (t.atMax()) t.progress = 0.f;
    return gained;
}

// Cut back by `steps` stages (never forward). Returns stages removed.
inline int TrimBack(GrowthTrack& t, int steps) {
    if (t.count <= 0) return 0;
    int target = std::max(0, t.stage - std::max(0, steps));
    int removed = t.stage - target;
    t.stage = target;
    t.progress = 0.f;
    return removed;
}

// Real bodies grow slower when starved or stressed.
inline float GrowthBodyFactor(float hungerLevel01, float thirstLevel01, float stress01, bool enabled) {
    if (!enabled) return 1.f;
    float f = 1.f - 0.35f * clamp01(hungerLevel01) - 0.15f * clamp01(thirstLevel01) - 0.20f * clamp01(stress01);
    return clampf(f, 0.35f, 1.f);
}

// --------------------------------------------------------------------------------------------
// Daily bookkeeping
// --------------------------------------------------------------------------------------------
struct DayStats {
    int day = -1;
    float fluidMl = 0;
    float portions = 0;
    int voids = 0;
    int bowel = 0;
    int brushed = 0;
};
inline void RollDay(DayStats& d, int today) {
    if (d.day != today) {
        d = DayStats{};
        d.day = today;
    }
}

// Stress delta per game hour for unrelieved needs (small, so that it feels like growing discomfort).
inline float DiscomfortStressPerHour(int urgencyLevel) {
    switch (urgencyLevel) {
    case 3: return 1.5f;
    case 4: return 4.0f;
    default: return 0.f;
    }
}
// Relief gives back a little stress, more the more urgent it was.
inline float ReliefStressBonus(float fracBefore, bool bowel) {
    float f = clamp01(fracBefore);
    return -((bowel ? 2.f : 1.f) + (bowel ? 6.f : 5.f) * f);
}

}  // namespace wgm
