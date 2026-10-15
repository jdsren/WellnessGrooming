#include "../src/Model.h"
#include <cstdio>
#include <cstdlib>
using namespace wgm;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

// Simulates `days` days: 8 drinks, 3 meals per day, player relieves at given fractions. Returns avg voids / bowel per day.
static void Sim(const char* name, int days, int drinks, int meals, float voidAt, float bowelAt, float& voidsPerDay, float& bowelPerDay, float& fluidPerDay) {
    NeedsParams p; IntakeParams ip; NeedsState s; StepContext ctx;
    int voids = 0, bm = 0; float fluid = 0;
    const float dt = 0.1f;  // hours
    for (int h10 = 0; h10 < days * 240; ++h10) {
        float hour = std::fmod(h10 * dt, 24.f);
        ctx.sleeping = (hour < 7.f);                      // asleep 00:00-07:00
        if (!ctx.sleeping) {
            for (int d = 0; d < drinks; ++d) {            // drinks spread over 07:00-23:00
                float at = 7.5f + d * (15.f / drinks);
                if (std::fabs(hour - at) < dt / 2) { ItemTraits t; t.drink = DrinkKind::NonAlcoholic; auto r = ComputeIntake(t, ip); AddFluid(s, r.fluidMl, r.diureticH); fluid += r.fluidMl; }
            }
            for (int m = 0; m < meals; ++m) {
                float at = 8.f + m * 5.f;
                if (std::fabs(hour - at) < dt / 2) { ItemTraits t; t.tier = FoodTier::Major; auto r = ComputeIntake(t, ip); AddFluid(s, r.fluidMl); AddFood(s, p, r.portions, 1.f, 0.f); fluid += r.fluidMl; }
            }
        }
        StepNeeds(s, p, dt, ctx);
        if (bladderFrac(s, p) >= voidAt && !ctx.sleeping) { s.bladder = 0; voids++; }
        if (bowelFrac(s) >= bowelAt && !ctx.sleeping) { s.colon = 0; bm++; }
        if (ctx.sleeping && bladderFrac(s, p) >= 1.f) { s.bladder = 0; voids++; } // night wake-up
    }
    voidsPerDay = voids / (float)days; bowelPerDay = bm / (float)days; fluidPerDay = fluid / days;
    printf("[%s] fluid %.0f ml/day, voids %.1f/day, bowel %.2f/day\n", name, fluidPerDay, voidsPerDay, bowelPerDay);
}

int main() {
    float v, b, f;
    Sim("normal: 8 drinks, 3 meals", 20, 8, 3, 0.70f, 0.75f, v, b, f);
    CHECK(v >= 4.5f && v <= 8.5f, "normal day gives 4.5-8.5 bladder visits");
    CHECK(b >= 0.7f && b <= 1.6f, "normal day gives about one bowel movement");
    Sim("thirsty: 2 drinks, 3 meals", 20, 2, 3, 0.70f, 0.75f, v, b, f);
    CHECK(v >= 1.5f && v <= 4.5f, "few drinks -> few visits");
    Sim("no food: 8 drinks, 0 meals", 20, 8, 0, 0.70f, 0.75f, v, b, f);
    CHECK(b < 0.2f, "no food -> no bowel pressure");
    Sim("no drinks: 0 drinks, 3 meals", 20, 0, 3, 0.70f, 0.75f, v, b, f);
    CHECK(v >= 1.0f && v <= 3.5f, "no drinks still has obligatory urine but few visits");

    // time-step independence: 1 big step vs many small steps
    { NeedsParams p; NeedsState a, c; AddFluid(a, 500); AddFluid(c, 500); AddFood(a, p, 3, 1, 0); AddFood(c, p, 3, 1, 0);
      StepContext x; StepNeeds(a, p, 8.f, x); for (int i = 0; i < 80; ++i) StepNeeds(c, p, 0.1f, x);
      CHECK(std::fabs(a.bladder - c.bladder) < 5.f && std::fabs(a.colon - c.colon) < 0.03f, "big step equals small steps"); }
    // fluid is conserved
    { NeedsParams p; p.basalMlPerH = 0; NeedsState s; AddFluid(s, 1000); StepContext x; StepNeeds(s, p, 24.f, x);
      CHECK(std::fabs(s.bladder - 600.f) < 15.f || s.bladder >= p.bladderCapMl * 1.2f, "absorbed fluid ends as urine share (or caps at full)"); }
    // diuretic
    { NeedsParams p; NeedsState a, b; AddFluid(a, 300); AddFluid(b, 300, 2.5f); StepContext x; StepNeeds(a, p, 2.f, x); StepNeeds(b, p, 2.f, x);
      CHECK(b.bladder > a.bladder * 1.3f, "alcohol makes the bladder fill faster"); }
    // dehydration
    { NeedsParams p; NeedsState a, b; AddFluid(a, 300); AddFluid(b, 300); StepContext n, d; d.hydration = 0.5f; StepNeeds(a, p, 3.f, n); StepNeeds(b, p, 3.f, d);
      CHECK(b.bladder < a.bladder * 0.7f, "dehydration reduces urine"); }
    // urgency thresholds
    { Thresholds t; CHECK(UrgencyLevel(0.2f, t) == 0 && UrgencyLevel(0.45f, t) == 1 && UrgencyLevel(0.75f, t) == 2 && UrgencyLevel(0.95f, t) == 3 && UrgencyLevel(1.0f, t) == 4, "urgency levels"); }
    // intake
    { IntakeParams ip; ItemTraits t; t.drink = DrinkKind::Wine; auto r = ComputeIntake(t, ip); CHECK(r.fluidMl == 150 && r.diureticH > 0, "wine glass");
      ItemTraits f; f.tier = FoodTier::Major; auto q = ComputeIntake(f, ip); CHECK(q.portions > 0.8f && q.fluidMl > 90, "major food gives portions + moisture");
      ItemTraits pot; pot.drink = DrinkKind::Potion; CHECK(!ComputeIntake(pot, ip).counted, "potions ignored by default"); }
    // growth
    { GrowthParams g; g.defaultDays = 8; GrowthTrack t; t.count = 18;
      int gained = AdvanceGrowth(t, g, 8.f * 3 + 4.f); CHECK(gained == 3 && t.stage == 3 && std::fabs(t.progress - 0.5f) < 0.01f, "growth: 28 days = 3 stages + 50 %");
      AdvanceGrowth(t, g, 1000.f); CHECK(t.stage == 17 && t.progress == 0.f, "growth stops at last stage");
      int removed = TrimBack(t, 2); CHECK(removed == 2 && t.stage == 15 && t.progress == 0, "trim steps back 2");
      TrimBack(t, 99); CHECK(t.stage == 0, "trim never below stage 1");
      GrowthTrack one; one.count = 1; CHECK(AdvanceGrowth(one, g, 100.f) == 0, "single style never grows");
      GrowthParams g2; g2.defaultDays = 8; g2.perStage[0] = 2; GrowthTrack u; u.count = 5; AdvanceGrowth(u, g2, 2.f); CHECK(u.stage == 1, "per-stage override");
      GrowthParams g3; g3.defaultDays = 10; g3.speed = 2.f; GrowthTrack w; w.count = 5; AdvanceGrowth(w, g3, 5.f); CHECK(w.stage == 1, "speed multiplier"); }
    CHECK(GrowthBodyFactor(0, 0, 0, true) == 1.f && GrowthBodyFactor(1, 1, 1, true) >= 0.35f && GrowthBodyFactor(1, 1, 1, true) < 0.5f && GrowthBodyFactor(1, 1, 1, false) == 1.f, "growth body factor");
    { DayStats d; d.day = 3; d.voids = 4; RollDay(d, 3); CHECK(d.voids == 4, "same day keeps stats"); RollDay(d, 4); CHECK(d.voids == 0 && d.day == 4, "new day resets"); }
    printf(fails ? "\nFAILED (%d)\n" : "\nPASSED\n", fails);
    return fails ? 1 : 0;
}
