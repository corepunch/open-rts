#include "game.h"
#include "info.h"
#include "dc_facing.h"
#include <stdlib.h>

void A_DC_TrooperAttackStart(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit) return;
    P_SetMobjState(ctx, unit, (rand() & 1) ? S_TRSC_ATKB1 : S_TRSC_ATK1);
}

void A_DC_Fall(statecontext_t *ctx, mobj_t *unit) {
    (void)ctx;
    if (!unit) return;
    P_MobjSetSelected(unit, false);
    unit->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                      MF_ATTACK | MF_HARVESTER);
    unit->movement.flow_field = NULL;
    unit->attack.target = -1;
    unit->harvest.target = -1;
    unit->harvest.timer_ms = 0;
    unit->harvest.phase = 0;
    unit->harvest.cargo = 0;
    unit->attack.cooldown_left_ms = 0;
    unit->core.momentum = fixed3_zero();
}

static int reaper_death_state_for_angle(angle_t angle) {
    /* Preserve the existing sparse-direction choice; retail selection is unknown. */
    int code = dc_angle_to_direction(angle);
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            if (suffix == 14) return S_REAP_DIEA14_1;
            if (suffix == 10) return S_REAP_DIEA10_1;
            if (suffix == 6) return S_REAP_DIEA6_1;
            if (suffix == 2) return S_REAP_DIEA2_1;
        }
    }
    return S_NULL;
}

void A_DC_ReaperDeath(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit) return;
    A_DC_Fall(ctx, unit);
    P_SetMobjState(ctx, unit, reaper_death_state_for_angle(unit->core.angle));
}

void A_DC_Corpse(statecontext_t *ctx, mobj_t *unit) {
    if (!unit) return;
    P_AddCorpse(ctx, unit);
    unit->core.momentum = fixed3_zero();
    unit->remove = true;
}
