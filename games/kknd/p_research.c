#include "game.h"
#include "kknd.h"
#include "info.h"

static bool ready(const mobj_t *u) {
    return u && !u->remove && u->hp > 0 && gameinfo->states[u->core.state_id].group != 6;
}
static bool is_lab(const mobj_t *u) {
    return u->type_id == MT_SURV_RESEARCH_LAB || u->type_id == MT_MUTE_ALCHEMY_HALL;
}

bool KK_Research(mobj_t *target) {
    if (!ready(target)) return false;
    int next = KK_NextTechLevel(target);
    mobj_t *lab = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (!ready(u) || u->owner != target->owner || !is_lab(u)) continue;
        if (u->research.target == target->id) {
            u->research.target = 0; /* Cancel: paid research is not refunded. */
            return true;
        }
        if (!u->research.target && !lab) lab = u;
    }
    if (!lab || !next) return false;
    static const int rates[] = {100,90,80,70,60,50};
    int rate = rates[lab->research.level];
    lab->research.target = target->id;
    lab->research.next_level = next;
    /* Preserve the reference's arithmetic order: only the per-level term is discounted. */
    lab->research.total_cost = lab->research.remaining_cost = 250 + 500 * next * rate / 100;
    lab->research.total_time = lab->research.remaining_time = 400 + 300 * next * rate / 100;
    lab->research.clock = 0;
    return true;
}

void A_KkndResearch(mobj_t *lab) {
    if (!lab->research.target) return;
    mobj_t *target = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->id == lab->research.target) { target = u; break; }
    }
    if (!ready(target) || target->owner != lab->owner) {
        lab->research.target = 0;
        return;
    }
    /* Reference normal speed is 25 tics/s; keep exact progress at our 30 Hz. */
    lab->research.clock += 25;
    if (lab->research.clock < RTS_TICRATE) return;
    lab->research.clock -= RTS_TICRATE;
    int remaining = lab->research.remaining_time == 1 ? 0 :
        lab->research.total_cost * lab->research.remaining_time / lab->research.total_time;
    int cost = lab->research.remaining_cost - remaining;
    if (level.player_resources[lab->owner][0] < cost) return;
    level.player_resources[lab->owner][0] -= cost;
    lab->research.remaining_cost -= cost;
    if (--lab->research.remaining_time == 0) {
        target->research.level = lab->research.next_level;
        lab->research.target = 0;
    }
}
