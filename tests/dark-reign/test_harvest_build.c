#include "../rts_model_test.h"
#include "../../driver/d_ticcmd.h"
#include "../../games/dark-reign/info.h"
#include "../../play/p_local.h"
#include "game.h"

#include <math.h>
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

static int vent_at_resource_cell(ivec2_t cell, int resource_type) {
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->resource_type != resource_type) continue;
        if (ivec2_equal(vent->cell, cell)) return i;
    }
    return -1;
}

static bool harvest_animating(const mobj_t *unit) {
    return unit && unit->core.state_id >= S_UCFRGST0_HARVEST1 &&
           unit->core.state_id <= S_UCFRGST0_HARVEST15;
}

static int test_dark_reign_transport_and_flight_traits(void) {
    const int transporters[] = {
        MT_FG_FREIGHTER, MT_FG_HOVER_FREIGHTER,
        MT_IMP_GROUND_TRANSPORTER, MT_IMP_HOVER_TRANSPORTER,
    };
    for (size_t i = 0; i < sizeof(transporters) / sizeof(transporters[0]); ++i) {
        if (!(mobjinfo[transporters[i]].flags & MF_HARVESTER))
            return fail("all ground and hover transporter types support harvesting");
    }
    if (mobjinfo[MT_FG_HOVER_FREIGHTER].flags & MF_FLY ||
        mobjinfo[MT_IMP_HOVER_TRANSPORTER].flags & MF_FLY)
        return fail("Hover Freighters use hover movement, not Fly movement");

    const int flyers[] = {
        MT_FG_SKY_BIKE, MT_FG_OUTRIDER, MT_IMP_RECON_SAUCER,
        MT_IMP_CYCLONE, MT_IMP_SKY_FORTRESS,
    };
    for (size_t i = 0; i < sizeof(flyers) / sizeof(flyers[0]); ++i) {
        if (!(mobjinfo[flyers[i]].flags & MF_FLY))
            return fail("all mapped retail Fly unit types have MF_FLY");
    }
    puts("PASS: all four transporter types harvest; mapped Fly types retain MF_FLY");
    return 0;
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

static bool on_vent(const mobj_t *unit, const resourcevent_t *vent) {
    if (!unit || !vent) return false;
    fvec2_t pos = fixed3_xy_to_fvec2(unit->core.position);
    return P_ResourceVentContainsCell(vent, (ivec2_t){ (int)floorf(pos.x), (int)floorf(pos.y) });
}

/* M01F: send the starting Freighter to a pit, attach, play harvest, leave for
 * the Water Launch Pad, deliver cargo, then spend it on a Construction Rig. */
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
    mobj_t *pad = find_owner_type(0, MT_FG_LIFE_PLANT);
    if (!harvester) return fail("starting Freighter");
    if (!hq) return fail("starting HQ");
    if (!pad) return fail("starting Water Launch Pad");
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
    /* Match the map-click path: TC_ORDER resolves a resource pit to harvesting. */
    ticcmd_t order = {
        .order = TC_ORDER,
        .position = fixed3_from_fvec2(pit_corner, 0),
        .count = 1,
        .units = { harvester->id },
    };
    G_RunTiccmd(0, &order);
    if (harvester->harvest.target != vent_index)
        return fail("harvest order attached the Freighter to the pit");
    if (harvester->harvest.phase != HARVEST_PHASE_TO_MINE ||
        harvester->movement.order_arrived || !harvester->movement.flow_field)
        return fail("map-click order started movement toward the pit");

    bool attached = false, mining = false, animating = false;
    int harvest_states_seen = 0;
    uint8_t harvest_seen[16] = { 0 };
    for (int t = 0; t < 30 * 90 &&
         harvester->harvest.phase != HARVEST_PHASE_TO_BASE; ++t) {
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
    }
    if (!attached) return fail("Freighter attached on the extractor footprint");
    if (!fvec2_near(fixed3_xy_to_fvec2(harvester->core.position), vent->attachment, 0.001f))
        return fail("Freighter parked at the pit attachment point");
    if (!mining) return fail("Freighter entered HARVEST_PHASE_MINING");
    if (!animating || harvest_states_seen != 15)
        return fail("Freighter played all 15 harvest frames before leaving");
    if (harvester->harvest.phase != HARVEST_PHASE_TO_BASE ||
        harvester->harvest.cargo != harvester->info->harvest.capacity)
        return fail("full cargo started a return trip");

    bool left_pit = false, reached_pad = false, unloaded = false;
    fvec2_t pad_pos = fixed3_xy_to_fvec2(pad->core.position);
    for (int t = 0; t < 30 * 180 && level.player_resources[0][0] < rig->cost; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick delivery");
        if (harvester->harvest.phase == HARVEST_PHASE_TO_BASE && !on_vent(harvester, vent))
            left_pit = true;
        if (left_pit &&
            fvec2_distance_squared(fixed3_xy_to_fvec2(harvester->core.position), pad_pos) < 6.0f * 6.0f)
            reached_pad = true;
        if (level.player_resources[0][0] > 0 && !left_pit)
            return fail("credits arrived while the Freighter was still on the pit");
        if (level.player_resources[0][0] > 0 && !reached_pad)
            return fail("Freighter unloaded before reaching the Water Launch Pad");
        if (level.player_resources[0][0] > 0) unloaded = true;
    }
    if (!left_pit) return fail("Freighter left the pit to return cargo");
    if (!reached_pad) return fail("Freighter reached the Water Launch Pad");
    if (!unloaded) return fail("Freighter unloaded cargo into player resources");
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

/* Retail FGGroundTransporter/FGHoverTransporter map impmn to fgpp. */
static int test_taelon_delivery_uses_power_generator(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN",
    };
    if (!model || !rts_game_model_load(model, &config)) return fail("load M01F for Taelon");

    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner != 0 && (unit->traits & MF_MOBILE)) P_RemoveMobj(unit);
    }
    RtsRenderSnapshot snap;
    if (!rts_tick(model, &snap)) return fail("tick after isolating Taelon test");

    mobj_t *freighter = player_harvester();
    mobj_t *generator = find_owner_type(0, MT_FG_POWER_PLANT);
    if (!freighter || !generator) return fail("find Freighter and FG power generator");
    int vent_index = vent_at_resource_cell((ivec2_t){3, 38}, 1);
    if (vent_index < 0) return fail("find Taelon mine paired with the FG power generator");
    const resourcevent_t *vent = &level.resource_vents[vent_index];
    freighter->core.position = fixed3_from_fvec2(vent->attachment, 0);

    int selected = rts_find_unit_by_id(&snap, freighter->id);
    RtsGameCommand select = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { selected, false } };
    if (selected < 0 || !rts_game_model_command(model, &select))
        return fail("select Freighter for Taelon");
    fvec2_t mine = { (float)vent->cell.x + 0.25f, (float)vent->cell.y + 0.25f };
    ticcmd_t order = {
        .order = TC_ORDER,
        .position = fixed3_from_fvec2(mine, 0),
        .count = 1,
        .units = { freighter->id },
    };
    G_RunTiccmd(0, &order);
    if (freighter->harvest.target != vent_index)
        return fail("TC_ORDER attached Freighter to Taelon mine");
    /* This mine overlaps the generator footprint on M01F, so begin the
     * movement assertion at its retail interaction point. The water test
     * covers movement from the starting position through attachment. */
    freighter->movement.flow_field = NULL;
    freighter->movement.order_arrived = true;

    for (int t = 0; t < 30 * 30 &&
         freighter->harvest.phase != HARVEST_PHASE_TO_BASE; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick Taelon gathering");
    }
    if (freighter->harvest.phase != HARVEST_PHASE_TO_BASE)
        return fail("full Taelon cargo started a return trip");
    if (!(generator->traits & MF_RESOURCE_BASE))
        return fail("power generator is a resource drop-off");
    if (fvec2_distance_squared(freighter->harvest.return_position,
            fixed3_xy_to_fvec2(generator->core.position)) > 6.0f * 6.0f)
        return fail("Freighter selected its Taelon power generator");

    int stock = level.player_resources[0][1];
    bool unloaded = false;
    for (int t = 0; t < 30 * 90 && !unloaded; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick Taelon delivery");
        unloaded = level.player_resources[0][1] > stock;
    }
    if (!unloaded) return fail("Freighter unloaded Taelon at the generator");
    if (freighter->harvest.phase != HARVEST_PHASE_TO_MINE ||
        freighter->harvest.target != vent_index)
        return fail("Freighter returned to its Taelon mine after unloading");

    puts("PASS: Taelon cargo returns to the compatible power generator");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_dark_reign_transport_and_flight_traits());
    RTS_RUN(test_gather_attach_animate_and_build());
    RTS_RUN(test_taelon_delivery_uses_power_generator());
    return 0;
}
