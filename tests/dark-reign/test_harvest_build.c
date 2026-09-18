#include "../rts_model_test.h"
#include "../../games/dark-reign/info.h"
#include "../../play/p_local.h"
#include "game.h"

#include <stdio.h>

static int fail(const char *msg) { return rts_fail("dark-reign", msg); }

static mobj_t *find_owner_type(uint8_t owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (!unit->remove && unit->hp > 0 && unit->owner == owner && unit->type_id == type)
            return unit;
    }
    return NULL;
}

static mobj_t *player_harvester(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (!unit->remove && unit->hp > 0 && unit->owner == 0 &&
            (unit->traits & MF_HARVESTER))
            return unit;
    }
    return NULL;
}

static int nearest_vent_index(fvec2_t position) {
    int best = -1;
    float best_dist2 = 1e30f;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (!vent->active || vent->amount <= 0) continue;
        float dist2 = fvec2_distance_squared(vent->attachment, position);
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

static bool harvest_animating(const mobj_t *unit) {
    return unit && unit->core.state_id >= S_UCFRGST0_HARVEST1 &&
           unit->core.state_id <= S_UCFRGST0_HARVEST15;
}

static int count_owner_type(uint8_t owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (mobj_t *)th;
        if (!unit->remove && unit->hp > 0 && unit->owner == owner && unit->type_id == type)
            n++;
    }
    return n;
}

/* M01F: send the starting Freighter to a pit, attach, play harvest, deliver
 * cargo to the HQ, then spend it on a Construction Rig. */
static int test_gather_attach_animate_and_build(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN",
    };
    if (!model || !rts_game_model_load(model, &config))
        return fail("load M01F");

    /* Keep the mission economy, but drop enemy mobiles so combat cannot
     * interrupt the Freighter on the way to the nearby extractor. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner != 0 && (unit->traits & MF_MOBILE))
            P_RemoveMobj(unit);
    }
    RtsRenderSnapshot snap;
    if (!rts_tick(model, &snap)) return fail("tick after isolating player");

    mobj_t *harvester = player_harvester();
    mobj_t *hq = find_owner_type(0, MT_FG_HQ1);
    if (!harvester) return fail("starting Freighter");
    if (!hq) return fail("starting HQ");
    int vent_index = nearest_vent_index(fixed3_xy_to_fvec2(harvester->core.position));
    if (vent_index < 0) return fail("extractor near Freighter");
    const resourcevent_t *vent = &level.resource_vents[vent_index];

    const StaticProductDefinition *rig = G_ModelProductByUIId(model, 11);
    if (!rig || rig->cost <= 0 || G_ModelActorIdForProduct(rig) != MT_FG_CONSTRUCTION_CREW)
        return fail("Construction Rig product");
    int rigs_before = count_owner_type(0, MT_FG_CONSTRUCTION_CREW);

    level.player_resources[0][0] = 0;
    RtsGameCommand build = {
        .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
        .data.build_product = { .producer_id = hq->id, .ui_id = rig->ui_id },
    };
    if (rts_game_model_command(model, &build))
        return fail("Construction Rig must be unaffordable before gathering");

    int harvester_index = rts_find_unit_by_id(&snap, harvester->id);
    if (harvester_index < 0) return fail("snapshot Freighter");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { harvester_index, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select Freighter");
    fvec2_t pit_corner = { (float)vent->cell.x + 0.25f, (float)vent->cell.y + 0.25f };
    RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = { .target = pit_corner } };
    if (!rts_game_model_command(model, &harvest))
        return fail("order Freighter to gather");
    if (harvester->harvest.target != vent_index)
        return fail("harvest order attached the Freighter to the pit");

    bool attached = false, mining = false, animating = false, cargo_flowed = false;
    int harvest_states_seen = 0;
    uint8_t harvest_seen[16] = { 0 };
    int cargo_at_attach = 0;
    for (int t = 0; t < 30 * 90; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick gather");
        fvec2_t pos = fixed3_xy_to_fvec2(harvester->core.position);
        if (harvester->harvest.target == vent_index &&
            P_ResourceVentContainsCell(vent, (ivec2_t){ (int)pos.x, (int)pos.y }))
            attached = true;
        if (harvester->harvest.phase == HARVEST_PHASE_MINING) mining = true;
        if (harvest_animating(harvester)) {
            animating = true;
            int frame = harvester->core.state_id - S_UCFRGST0_HARVEST1;
            if (frame >= 0 && frame < 15 && !harvest_seen[frame]) {
                harvest_seen[frame] = 1;
                harvest_states_seen++;
            }
        }
        if (attached && mining && animating && !cargo_flowed) {
            cargo_at_attach = harvester->harvest.cargo;
            cargo_flowed = true;
        }
        if (cargo_flowed &&
            (harvester->harvest.cargo > cargo_at_attach ||
             level.player_resources[0][0] > 0) &&
            harvest_states_seen >= 2)
            break;
    }
    if (!attached) return fail("Freighter attached on the extractor footprint");
    if (!mining) return fail("Freighter entered HARVEST_PHASE_MINING");
    if (!animating || harvest_states_seen < 2)
        return fail("Freighter played harvest animation");
    if (!(harvester->harvest.cargo > cargo_at_attach || level.player_resources[0][0] > 0))
        return fail("resources started flowing at the pit");

    for (int t = 0; t < 30 * 180 && level.player_resources[0][0] < rig->cost; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick delivery");
    }
    if (level.player_resources[0][0] < rig->cost)
        return fail("Freighter delivered enough credits for a Construction Rig");

    int stock = level.player_resources[0][0];
    if (!rts_game_model_command(model, &build))
        return fail("queue Construction Rig from gathered credits");
    if (level.player_resources[0][0] != stock - rig->cost)
        return fail("Construction Rig cost deducted");
    if (!hq->production || hq->production->queue_count < 1)
        return fail("HQ queued the Construction Rig");

    bool spawned = false;
    for (int t = 0; t < 30 * 30 && !spawned; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick training");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev)) {
            if (ev.type == RTS_GAME_EVENT_UNIT_BUILT &&
                ev.subject_type_id == MT_FG_CONSTRUCTION_CREW)
                spawned = true;
        }
        if (count_owner_type(0, MT_FG_CONSTRUCTION_CREW) > rigs_before)
            spawned = true;
    }
    if (!spawned || count_owner_type(0, MT_FG_CONSTRUCTION_CREW) <= rigs_before)
        return fail("Construction Rig trained from gathered resources");

    printf("PASS: Freighter attached, harvested, delivered %d credits, built Construction Rig\n",
           stock);
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_gather_attach_animate_and_build());
    return 0;
}
