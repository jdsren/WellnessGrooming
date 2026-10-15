#include "WG.h"

namespace WG {

namespace {
    enum class Phase { Idle, FadeOut, Start, Loop, End, FadeIn };

    struct Ctx {
        Phase ph = Phase::Idle;
        ReliefKind kind = ReliefKind::Bladder;
        bool automatic = false;
        float t = 0.f;            // time in the current phase
        float total = 0.f;        // time since the start
        float dur = 10.f;
        float fracBefore = 0.f;
        float alpha = 0.f;
        bool locked = false;
        bool wasFirstPerson = false;
        bool wasSneaking = false;
        bool usedOriginal = false;
        bool crouched = false;
        bool loopSent = false;
        bool entered = false;     // first frame of the phase handled
        std::string prefix;       // original-mod event prefix, e.g. pee_squatting
        std::string single;       // single-event pose (Nature Calls)
        std::string status = "idle";
    } c;

    bool g_origSeen = false, g_builtSeen = false;

    bool Send(const std::string& ev) {
        auto* pl = Player();
        return pl && pl->NotifyAnimationGraph(ev.c_str());
    }

    bool LocationBlocked() {
        if (g_cfg.allowInTowns) return false;
        auto* pl = Player();
        auto* loc = pl ? pl->GetCurrentLocation() : nullptr;
        if (!loc) return false;
        for (auto* kw : {"LocTypeCity", "LocTypeTown", "LocTypeHabitation", "LocTypeInn", "LocTypePlayerHouse"})
            if (loc->HasKeywordString(kw)) return true;
        return false;
    }

    void ChoosePose() {
        c.prefix.clear(); c.single.clear();
        if (c.kind == ReliefKind::Bladder) {
            int p = g_cfg.bladderPose;
            if (p == 0) p = PlayerIsFemale() ? 4 : 1;
            static const char* names[] = {"", "pee_standing", "pee_standing1h", "pee_stooping", "pee_squatting", "pee_kneeling"};
            c.prefix = names[std::clamp(p, 1, 5)];
        } else {
            switch (g_cfg.bowelPose) {
            case 1: c.prefix = "pee_kneeling"; break;
            case 2: c.prefix = "pee_stooping"; break;
            case 3: c.single = "gia_ZazAPC202"; break;
            default: c.prefix = "pee_squatting"; break;
            }
        }
    }
    bool StandingPose() { return c.prefix == "pee_standing" || c.prefix == "pee_standing1h"; }

    bool TryOriginal() {
        bool ok = false;
        if (!c.single.empty()) ok = Send(c.single);
        else ok = Send(c.prefix + "_start");
        if (ok) { c.usedOriginal = true; g_origSeen = true; }
        return ok;
    }
    bool TryBuiltin() {
        auto* pl = Player();
        if (!pl) return false;
        if (!StandingPose() && !pl->IsSneaking()) {
            if (Send("SneakStart")) { c.crouched = true; g_builtSeen = true; return true; }
            return false;
        }
        g_builtSeen = true;   // standing still is the built-in standing pose
        return true;
    }

    void BeginAnimation() {
        ChoosePose();
        bool ok = false;
        switch (g_cfg.animSource) {
        case 0: ok = TryBuiltin() || TryOriginal(); break;
        case 1: ok = TryOriginal() || TryBuiltin(); break;
        case 2: ok = TryOriginal(); break;
        case 3: ok = TryBuiltin(); break;
        default: ok = true; break;
        }
        c.status = c.usedOriginal ? "playing animation from the original mod" : (c.crouched ? "built-in crouch" : "standing");
        (void)ok;
    }

    void EndAnimation() {
        if (c.usedOriginal) {
            if (c.single.empty()) Send(c.prefix + "_end");
            Send("reset_to_idle");
            Send("IdleForceDefaultState");
        }
        if (c.crouched) { Send("SneakStop"); c.crouched = false; }
    }

    void Unlock() {
        if (c.locked) { SetPlayerControls(true); c.locked = false; }
        if (c.wasFirstPerson && g_cfg.forceThirdPerson) {
            if (auto* cam = RE::PlayerCamera::GetSingleton()) cam->ForceFirstPerson();
        }
        c.wasFirstPerson = false;
    }

    void Finish(bool completed) {
        auto* pl = Player();
        if (!pl) { c = Ctx{}; return; }
        if (completed) {
            const bool bowel = c.kind == ReliefKind::Bowel;
            RollDay(g_st.day, (int)DaysNow());
            if (bowel) {
                g_st.needs.colon = 0.f;
                g_st.day.bowel++; g_st.lastBowelDays = DaysNow();
            } else {
                g_st.needs.bladder = 0.f;
                g_st.day.voids++; g_st.lastVoidDays = DaysNow();
            }
            g_st.fullHours = 0.f;
            if (g_cfg.stressFromNeeds) AddStress(ReliefStressBonus(c.fracBefore, bowel), "relief");
            Notify(bowel ? "That is a relief." : "Ahh, much better.");
        }
    }
}

bool ReliefActive() { return c.ph != Phase::Idle; }
float FadeAlpha() { return c.alpha; }
std::string ReliefStatus() { return c.ph == Phase::Idle ? "idle" : c.status; }
bool OriginalAnimationsSeen() { return g_origSeen; }
bool BuiltInAnimationSeen() { return g_builtSeen; }

bool CanRelieveNow(std::string* why) {
    auto set = [&](const char* m) { if (why) *why = m; return false; };
    auto* pl = Player();
    if (!pl || !pl->Is3DLoaded()) return set("not ready");
    if (GameBusy()) return set("a menu is open");
    if (ReliefActive()) return set("already busy");
    if (pl->IsInCombat()) return set("you are in combat");
    if (pl->IsOnMount()) return set("you are on a mount");
    if (pl->IsDead() || pl->IsInRagdollState() || pl->IsInKillMove()) return set("not possible right now");
    if (pl->IsInWater()) return set("you are in water");
    auto* as = pl->AsActorState();
    if (!as) return set("not ready");
    if (as->IsSwimming() || as->IsFlying() || as->IsBleedingOut()) return set("not possible right now");
    if (as->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) return set("you are sitting or sleeping");
    if (as->GetWeaponState() != RE::WEAPON_STATE::kSheathed) return set("sheathe your weapon first");
    if (auto* cm = RE::ControlMap::GetSingleton()) if (!cm->IsMovementControlsEnabled()) return set("your controls are locked by a scene");
    if (LocationBlocked()) return set("not in a town or house");
    return true;
}

bool StartRelief(ReliefKind k, bool automatic) {
    if (!g_st.active) { if (!automatic) Notify("Wellness Grooming is not started yet (SKSE menu)."); return false; }
    const float frac = (k == ReliefKind::Bladder) ? bladderFrac(g_st.needs, g_cfg.needs) : bowelFrac(g_st.needs);
    if (frac < 0.12f) { if (!automatic) Notify(k == ReliefKind::Bladder ? "You do not need to pee." : "You do not need to go."); return false; }
    std::string why;
    if (!CanRelieveNow(&why)) { if (!automatic) Notify("Not now: " + why + "."); return false; }
    c = Ctx{};
    c.kind = k;
    c.automatic = automatic;
    c.fracBefore = frac;
    c.dur = std::max(2.f, k == ReliefKind::Bladder ? g_cfg.reliefSecBladder : g_cfg.reliefSecBowel);
    c.ph = g_cfg.blackScreen ? Phase::FadeOut : Phase::Start;
    c.status = "starting";
    return true;
}

void CancelRelief(const char* why) {
    if (c.ph == Phase::Idle) { c.alpha = 0.f; return; }
    Log(std::string("relief cancelled: ") + (why ? why : ""));
    EndAnimation();
    Unlock();
    c = Ctx{};
}

void UpdateRelief(float dt) {
    if (c.ph == Phase::Idle) { if (c.alpha > 0.f) c.alpha = std::max(0.f, c.alpha - dt / std::max(0.1f, g_cfg.fadeSeconds)); return; }
    auto* pl = Player();
    if (!pl || !pl->Is3DLoaded()) { c = Ctx{}; return; }
    c.t += dt; c.total += dt;
    if (c.total > 150.f) { CancelRelief("timeout"); return; }
    if (c.ph != Phase::FadeIn && (pl->IsInCombat() || pl->IsDead() || pl->IsInRagdollState())) { CancelRelief("combat"); return; }
    const float fade = std::max(0.1f, g_cfg.fadeSeconds);

    switch (c.ph) {
    case Phase::FadeOut:
        c.alpha = std::min(1.f, c.alpha + dt / fade);
        c.status = "fading out";
        if (c.alpha >= 1.f) { c.ph = Phase::Start; c.t = 0.f; c.entered = false; }
        break;
    case Phase::Start: {
        if (!c.entered) {
            c.entered = true;
            // first frame of the phase: lock controls, camera, animation
            if (!c.locked) { SetPlayerControls(false); c.locked = true; }
            if (g_cfg.forceThirdPerson && !g_cfg.blackScreen) {
                if (auto* cam = RE::PlayerCamera::GetSingleton()) {
                    c.wasFirstPerson = cam->IsInFirstPerson();
                    if (c.wasFirstPerson) cam->ForceThirdPerson();
                }
            }
            BeginAnimation();
        }
        if (c.t >= (c.usedOriginal ? 1.6f : 0.9f)) { c.ph = Phase::Loop; c.t = 0.f; c.entered = false; }
        break;
    }
    case Phase::Loop:
        if (!c.loopSent) {
            if (c.usedOriginal && c.single.empty()) Send(c.prefix + "_loop");
            c.loopSent = true;
        }
        c.status = c.kind == ReliefKind::Bladder ? "relieving yourself" : "answering nature's call";
        if (c.t >= c.dur) { c.ph = Phase::End; c.t = 0.f; c.entered = false; }
        break;
    case Phase::End:
        if (!c.entered) { c.entered = true; EndAnimation(); }
        c.status = "finishing";
        if (c.t >= 1.3f) {
            Unlock();
            Finish(true);
            if (g_cfg.blackScreen) { c.ph = Phase::FadeIn; c.t = 0.f; c.entered = false; }
            else { c = Ctx{}; }
        }
        break;
    case Phase::FadeIn:
        c.alpha = std::max(0.f, c.alpha - dt / fade);
        c.status = "fading in";
        if (c.alpha <= 0.f) c = Ctx{};
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- reminders, stress, auto relief, accidents
void OnNeedsTick(float hours) {
    auto& s = g_st;
    const auto& cfg = g_cfg;
    const float now = DaysNow();
    const float bl = bladderFrac(s.needs, cfg.needs), bw = bowelFrac(s.needs);
    const int lb = UrgencyLevel(bl, cfg.urge), lw = UrgencyLevel(bw, cfg.urge);
    int lo = 0;
    if (cfg.oralEnabled) lo = s.needs.oral >= 0.95f ? 3 : (s.needs.oral >= 0.75f ? 2 : (s.needs.oral >= 0.5f ? 1 : 0));

    static const char* bladderMsg[] = {"", "You could use a trip to the privy.", "Your bladder is getting uncomfortable.", "You really need to pee, badly!", "You cannot hold it any longer!"};
    static const char* bowelMsg[] = {"", "Your stomach rumbles. Nature may call soon.", "You feel an urgent pressure below.", "You desperately need to go!", "You cannot hold it any longer!"};
    static const char* oralMsg[] = {"", "Your mouth feels a bit stale.", "Your teeth feel furry. Time to brush them.", "Your teeth are filthy and your breath is foul."};

    auto remind = [&](int idx, int level, int& last, const char* msg) {
        if (level > last && level > 0 && (now - s.lastNotifyDays[idx]) * 1440.f >= cfg.notifyCooldownMin) {
            Notify(msg);
            s.lastNotifyDays[idx] = now;
        }
        last = level;
    };
    remind(0, lb, s.lastUrgency[0], bladderMsg[lb]);
    remind(1, lw, s.lastUrgency[1], bowelMsg[lw]);
    remind(2, lo, s.lastUrgency[2], oralMsg[lo]);

    if (cfg.stressFromNeeds) {
        float st = DiscomfortStressPerHour(lb) + DiscomfortStressPerHour(lw);
        if (cfg.oralEnabled && cfg.oralStress && lo >= 3) st += 0.3f;
        if (st > 0.f) AddStress(st * hours, "discomfort");
    }

    // too late: accident
    if (std::max(lb, lw) >= 4) s.fullHours += hours; else s.fullHours = 0.f;
    if (cfg.accidentHours > 0.f && s.fullHours >= cfg.accidentHours && !ReliefActive()) {
        const bool bowel = lw >= 4 && lw >= lb;
        if (bowel) { s.needs.colon = 0.3f; Notify("You could not hold it and soiled yourself."); }
        else { s.needs.bladder = cfg.needs.bladderCapMl * 0.1f; Notify("You could not hold it and wet yourself."); }
        s.fullHours = 0.f;
        if (cfg.stressFromNeeds) AddStress(10.f, "accident");
        return;
    }

    // go by yourself when it is getting desperate and the place is safe
    if (cfg.autoRelief == 1 && !ReliefActive() && !GameBusy()) {
        if (lw >= 3 && lw >= lb) StartRelief(ReliefKind::Bowel, true);
        else if (lb >= 3) StartRelief(ReliefKind::Bladder, true);
    }
}

}  // namespace WG
