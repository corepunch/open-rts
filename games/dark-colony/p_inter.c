#include "game.h"
#include "info.h"
#include "dc_types.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void A_DC_MuzzleFlash(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit) return;
    int muzzle_state = 0;
    if (ctx->game_info && unit->type_id > 0 &&
        unit->type_id < ctx->game_info->mobj_type_count) {
        muzzle_state = ctx->game_info->mobjinfo[unit->type_id].muzzleflash;
    }
    P_SpawnEffect(ctx, muzzle_state, unit->gx, unit->gy, unit->facing_code);
}

void A_DC_Attack(statecontext_t *ctx, mobj_t *unit) {
    P_Attack(ctx, unit);
}

void A_DC_TrooperAttackStart(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit) return;
    P_SetMobjState(ctx, unit, (rand() & 1) ? S_DC_TRSC_ATKB1 : S_DC_TRSC_ATK1);
}

void A_DC_Fall(statecontext_t *ctx, mobj_t *unit) {
    (void)ctx;
    if (!unit) return;
    unit->selected = false;
    unit->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                      MF_ATTACK | MF_HARVESTER);
    unit->path_len = 0;
    unit->path_index = 0;
    unit->attack_target = -1;
    unit->harvest_target = -1;
    unit->harvest_timer_ms = 0;
    unit->attack_cooldown_left_ms = 0;
    unit->attack_anim_left_ms = 0;
    unit->death_started = true;
}

static int reaper_death_effect_state_for_facing(int facing_code) {
    int code = facing_code & 15;
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            if (suffix == 14) return S_DC_REAP_DIEA14_FX1;
            if (suffix == 6) return S_DC_REAP_DIEA6_FX1;
            if (suffix == 10 || suffix == 2) return S_NULL;
        }
    }
    return S_NULL;
}

void A_DC_ReaperDeath(statecontext_t *ctx, mobj_t *unit) {
    if (ctx && unit) {
        int fx_state = reaper_death_effect_state_for_facing(unit->facing_code);
        if (fx_state != S_NULL) {
            P_SpawnEffect(ctx, fx_state, unit->gx, unit->gy, unit->facing_code);
        }
    }
    A_DC_Fall(ctx, unit);
}

void A_DC_Corpse(statecontext_t *ctx, mobj_t *unit) {
    if (!unit) return;
    P_AddCorpse(ctx, unit);
    unit->remove = true;
}
