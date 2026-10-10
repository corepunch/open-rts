#include "t_local.h"
#include "engine.h"
#include "../../games/dark-reign/info.h"
#include "dark-reign.h"

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

static int nearest_vent_index(fixed2_t position) {
    int best = -1;
    int64_t best_dist2 = INT64_MAX;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (!vent->active || vent->amount <= 0) continue;
        int64_t dist2 = fixed2_distance_squared64(vent->attachment, position);
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
    return unit && unit->core.state_id == S_UCFRGST0_HARVEST1;
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
    fixed2_t pos = fixed3_xy(unit->core.position);
    return P_ResourceVentContainsCell(vent, fixed2_cell(pos));
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
    int vent_index = nearest_vent_index(fixed3_xy(harvester->core.position));
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
    fixed2_t pit_corner = { FIXED_FROM_INT(vent->cell.x) + FIXED_LIT(0.25), FIXED_FROM_INT(vent->cell.y) + FIXED_LIT(0.25) };
    /* Match the map-click path: TC_ORDER resolves a resource pit to harvesting. */
    ticcmd_t order = {
        .order = TC_ORDER,
        .position = fixed3_from_fixed2(pit_corner, 0),
        .count = 1,
        .units = { harvester->id },
    };
    G_RunTiccmd(0, &order);
    if (harvester->harvest.target != vent_index)
        return fail("harvest order attached the Freighter to the pit");
    if (harvester->harvest.phase != HARVEST_PHASE_TO_MINE ||
        harvester->movement.order_arrived || !harvester->movement.path.count)
        return fail("map-click order started movement toward the pit");

    bool attached = false, mining = false, animating = false;
    int harvest_states_seen = 0;
    uint8_t harvest_seen[16] = { 0 };
    for (int t = 0; t < 30 * 90 &&
         harvester->harvest.phase != HARVEST_PHASE_TO_BASE; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick gather");
        fixed2_t pos = fixed3_xy(harvester->core.position);
        if (harvester->harvest.target == vent_index &&
            P_ResourceVentContainsCell(vent, fixed2_cell(pos)))
            attached = true;
        if (harvester->harvest.phase == HARVEST_PHASE_MINING) mining = true;
        if (harvest_animating(harvester)) {
            animating = true;
            int frame = harvester->core.state_frame;
            if (frame >= 0 && frame < 15 && !harvest_seen[frame]) {
                harvest_seen[frame] = 1;
                harvest_states_seen++;
            }
        }
    }
    if (!attached) return fail("Freighter attached on the extractor footprint");
    if (!fixed2_near(fixed3_xy(harvester->core.position), vent->attachment, FIXED_LIT(0.001)))
        return fail("Freighter parked at the pit attachment point");
    if (!mining) return fail("Freighter entered HARVEST_PHASE_MINING");
    if (!animating || harvest_states_seen != 15)
        return fail("Freighter played all 15 harvest frames before leaving");
    if (harvester->harvest.phase != HARVEST_PHASE_TO_BASE ||
        harvester->harvest.cargo != harvester->info->harvest.resources[0].capacity)
        return fail("full cargo started a return trip");

    bool left_pit = false, reached_pad = false, unloaded = false;
    fixed2_t pad_bay = fixed2_add(fixed3_xy(pad->core.position), FIXED2_LIT(3.5, 2.5));
    int credited_resources = level.player_resources[0][0];
    for (int t = 0; t < 30 * 180 && level.player_resources[0][0] < rig->cost; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick delivery");
        if (harvester->harvest.phase == HARVEST_PHASE_TO_BASE && !on_vent(harvester, vent))
            left_pit = true;
        if (level.player_resources[0][0] > credited_resources) {
            fixed2_t position = fixed3_xy(harvester->core.position);
            if (fixed2_distance_squared64(position, harvester->harvest.return_position) > FIXED_LIT_64(0.0001))
                return fail("Freighter unloaded before reaching its return goal");
            if (!fixed2_near(position, pad_bay, FIXED_LIT(0.001)))
                return fail("Freighter delivered at the native Water Launch Pad bay");
            credited_resources = level.player_resources[0][0];
            reached_pad = true;
        }
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

    int selected = rts_find_unit_by_id(&snap, freighter->id);
    RtsGameCommand select = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { selected, false } };
    if (selected < 0 || !rts_game_model_command(model, &select))
        return fail("select Freighter for Taelon");
    fixed2_t mine = { FIXED_FROM_INT(vent->cell.x) + FIXED_LIT(0.25), FIXED_FROM_INT(vent->cell.y) + FIXED_LIT(0.25) };
    ticcmd_t order = {
        .order = TC_ORDER,
        .position = fixed3_from_fixed2(mine, 0),
        .count = 1,
        .units = { freighter->id },
    };
    G_RunTiccmd(0, &order);
    if (freighter->harvest.target != vent_index)
        return fail("TC_ORDER attached Freighter to Taelon mine");
    for (int t = 0; t < 30 * 90 &&
         freighter->harvest.phase != HARVEST_PHASE_TO_BASE; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick Taelon gathering");
    }
    if (freighter->harvest.phase != HARVEST_PHASE_TO_BASE)
        return fail("full Taelon cargo started a return trip");
    if (!(generator->traits & MF_RESOURCE_BASE))
        return fail("power generator is a resource drop-off");
    fixed2_t bay = fixed2_add(fixed3_xy(generator->core.position), FIXED2_LIT(1.5, 3.5));
    if (!fixed2_near(freighter->harvest.return_position, bay, FIXED_LIT(0.001)))
        return fail("Freighter selected the native Taelon generator bay");

    int stock = level.player_resources[0][1];
    /* Keep cargo aboard while the return movement is still active. */
    if (!rts_tick(model, &snap)) return fail("tick Taelon return approach");
    if (level.player_resources[0][1] != stock ||
        freighter->harvest.phase != HARVEST_PHASE_TO_BASE)
        return fail("Taelon cargo stayed aboard until the return path arrived");
    bool unloaded = false;
    for (int t = 0; t < 30 * 90 && !unloaded; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick Taelon delivery");
        if (level.player_resources[0][1] > stock) {
            if (fixed2_distance_squared64(fixed3_xy(freighter->core.position),
                                       freighter->harvest.return_position) > FIXED_LIT_64(0.0001))
                return fail("Taelon cargo unloaded before reaching the power generator");
            unloaded = freighter->harvest.cargo == 0;
        }
    }
    if (!unloaded) return fail("Freighter unloaded Taelon at the generator");
    if (freighter->harvest.phase != HARVEST_PHASE_TO_MINE ||
        freighter->harvest.target != vent_index)
        return fail("Freighter returned to its Taelon mine after unloading");

    puts("PASS: Taelon cargo returns to the compatible power generator");
    rts_game_model_destroy(model);
    return 0;
}

static int test_all_transporter_deliveries(void) {
    const int types[] = {MT_FG_FREIGHTER, MT_FG_HOVER_FREIGHTER,
                        MT_IMP_GROUND_TRANSPORTER, MT_IMP_HOVER_TRANSPORTER};
    const fixed2_t bays[] = {FIXED2_LIT(7.5, 55.5), FIXED2_LIT(6.5, 42.5)};
    for (unsigned type = 0; type < sizeof(types) / sizeof(*types); ++type) {
        for (int resource = 0; resource < 2; ++resource) {
            RtsGameModel *model = rts_game_model_create();
            RtsGameModelConfig config = {.data_root = "data/REIGN/dark",
                .map_path = "scenario/FIXED/M01F/M01F.SCN"};
            if (!model || !rts_game_model_load(model, &config)) return fail("load delivery matrix");
            mobj_t *initial = player_harvester();
            if (!initial) return fail("matrix starting Freighter");
            fixed3_t start = initial->core.position;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                if (th->function != P_MobjThinker) continue;
                mobj_t *unit = (mobj_t *)th;
                if (unit->traits & MF_MOBILE) P_RemoveMobj(unit);
            }
            mobj_t *unit = P_SpawnMobj(start, types[type]);
            if (!unit) return fail("spawn matrix transporter");
            int target = resource ? vent_at_resource_cell((ivec2_t){3,38}, 1) :
                nearest_vent_index(fixed3_xy(start));
            if (target < 0 || level.resource_vents[target].resource_type != resource)
                return fail("matrix resource node");
            resourcevent_t *vent = &level.resource_vents[target];
            if (!P_HarvestUnitTo(&level, unit, vent->attachment)) return fail("order matrix transporter");
            int capacity = resource ? 50 : 750;
            int batch = resource ? 25 : 270;
            int initial_stock = level.player_resources[0][resource];
            int delivered = 0, cycles = 0, animation_tics = 0;
            unsigned frames = 0;
            for (int t = 0; t < 30 * 180 && delivered < 2 * capacity; ++t) {
                int previous_cargo = unit->harvest.cargo;
                int previous_stock = level.player_resources[0][resource];
                bool turning = unit->harvest.phase == HARVEST_PHASE_UNLOAD_TURNING ||
                               unit->harvest.phase == HARVEST_PHASE_TURNING;
                angle_t remaining = angle_distance(unit->core.angle, ANG90 + ANG45);
                if (unit->harvest.phase == HARVEST_PHASE_UNLOADING) {
                    int frame = unit->core.state_frame;
                    if (unit->core.state_id != unit->info->harvest.unload_state_id || frame >= 15)
                        return fail("native unloading frame range");
                    frames |= 1u << frame;
                    animation_tics++;
                    if (!fixed2_near(fixed3_xy(unit->core.position), bays[resource], FIXED_LIT(0.001)) ||
                        unit->core.angle != ANG90 + ANG45)
                        return fail("unloading stays at the authored bay and facing");
                }
                if (!rts_tick(model, NULL)) return fail("tick matrix delivery");
                if (turning) {
                    angle_t step = (UINT32_C(0x1000000) * 10 / 360) << 8;
                    angle_t expected = remaining > step ? remaining - step : 0;
                    if (angle_distance(unit->core.angle, ANG90 + ANG45) != expected)
                        return fail("native ten-degree turning step and exact final clamp");
                }
                int credit = level.player_resources[0][resource] - previous_stock;
                if (credit) {
                    int expected = previous_cargo < batch ? previous_cargo : batch;
                    if (credit != expected || unit->harvest.cargo != previous_cargo - expected)
                        return fail("retail unload batch and cargo conservation");
                    if (frames != 0x7fff || animation_tics != 51)
                        return fail("credit follows all 15 native frames and 51 simulation tics");
                    if (!fixed2_near(fixed3_xy(unit->core.position), bays[resource], FIXED_LIT(0.001)))
                        return fail("credit at exact native bay");
                    frames = 0;
                    animation_tics = 0;
                    delivered += credit;
                    cycles++;
                }
            }
            if (delivered != 2 * capacity || cycles != (resource ? 4 : 6) ||
                level.player_resources[0][resource] != initial_stock + delivered ||
                unit->harvest.phase != HARVEST_PHASE_TO_MINE || unit->harvest.target != target)
                return fail("all transporters complete two full native delivery trips");
            printf("PASS: transporter=%d resource=%d bay=(%.1f,%.1f) cycles=%d delivered=%d\n",
                   types[type], resource, fixed_to_float(bays[resource].x), fixed_to_float(bays[resource].y), cycles, delivered);
            rts_game_model_destroy(model);
        }
    }
    return 0;
}

static int test_delivery_requires_live_accessible_bay(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN"};
    if (!model || !rts_game_model_load(model, &config)) return fail("load unavailable bay test");
    mobj_t *unit = player_harvester();
    mobj_t *pad = find_owner_type(0, MT_FG_LIFE_PLANT);
    if (!unit || !pad) return fail("unavailable bay actors");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *other = (mobj_t *)th;
        if (other != unit && (other->traits & MF_MOBILE)) P_RemoveMobj(other);
    }
    const dr_mission_t *mission = level.mission;
    const int pads[] = {MT_FG_LIFE_PLANT, MT_IMP_LIFE_PLANT};
    const int generators[] = {MT_FG_POWER_PLANT, MT_IMP_POWER_PLANT};
    for (int i = 0; i < 2; ++i) {
        if (!ivec2_equal(mission->bays[pads[i]], (ivec2_t){3,2}) ||
            !ivec2_equal(mission->bays[generators[i]], (ivec2_t){1,3}))
            return fail("both factions load native SetBay coordinates");
    }
    const int pad_mask[4][5] = {{-1,-1,-1,-1,-1}, {-1,2,3,3,2},
                              {3,3,3,2,2}, {2,3,3,2,-1}};
    ivec2_t origin = fixed2_cell(fixed3_xy(pad->core.position));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            if (pad_mask[y][x] != -1 && L_IsWalkable(&level, origin.x+x, origin.y+y) != (pad_mask[y][x] == 2))
                return fail("OVLEFF preserves every solid and walkable launch-pad cell");
    int target = nearest_vent_index(fixed3_xy(unit->core.position));
    if (target < 0 || !P_HarvestUnitTo(&level, unit, level.resource_vents[target].attachment))
        return fail("unavailable bay harvest order");
    for (int t = 0; t < 30 * 90 && unit->harvest.phase != HARVEST_PHASE_TO_BASE; ++t)
        if (!rts_tick(model, NULL)) return fail("unavailable bay gather");
    if (unit->harvest.phase != HARVEST_PHASE_TO_BASE || unit->harvest.cargo != 750)
        return fail("unavailable bay full cargo");
    int stock = level.player_resources[0][0];
    fixed3_t position = pad->core.position;
    P_RemoveMobj(pad);
    for (int t = 0; t < 90; ++t)
        if (!rts_tick(model, NULL)) return fail("tick destroyed pad");
    if (unit->harvest.base || unit->harvest.cargo != 750 || level.player_resources[0][0] != stock)
        return fail("destroyed pad never receives cargo or leaves a dangling reference");
    pad = P_SpawnMobj(position, MT_IMP_LIFE_PLANT);
    if (!pad) return fail("replacement Imperium pad");
    int bay_cell = L_Index(&level, 7, 55);
    level.blocked[bay_cell] = 1;
    for (int t = 0; t < 90; ++t)
        if (!rts_tick(model, NULL)) return fail("tick blocked bay");
    if (unit->harvest.cargo != 750 || level.player_resources[0][0] != stock)
        return fail("blocked bay never substitutes a nearby unloading point");
    level.blocked[bay_cell] = 0;
    for (int t = 0; t < 30 * 90 && unit->harvest.cargo; ++t)
        if (!rts_tick(model, NULL)) return fail("tick replacement delivery");
    if (unit->harvest.cargo || level.player_resources[0][0] != stock + 750)
        return fail("retarget replacement pad and resume the complete delivery");
    puts("PASS: native footprints and bays; destroyed/blocked drop-offs retain cargo and recover");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shared_bay_and_interrupted_unload(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN"};
    if (!model || !rts_game_model_load(model, &config)) return fail("load shared bay test");
    mobj_t *first = player_harvester();
    if (!first) return fail("shared bay Freighter");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit != first && (unit->traits & MF_MOBILE)) P_RemoveMobj(unit);
    }
    int target = nearest_vent_index(fixed3_xy(first->core.position));
    if (target < 0) return fail("shared pit");
    fixed2_t pit = level.resource_vents[target].attachment;
    if (!P_HarvestUnitTo(&level, first, pit)) return fail("order shared pit");
    for (int t = 0; t < 30 * 90 && first->harvest.phase != HARVEST_PHASE_UNLOADING; ++t)
        if (!rts_tick(model, NULL)) return fail("reach shared dock");
    if (first->harvest.phase != HARVEST_PHASE_UNLOADING) return fail("first transporter docking");
    int stock = level.player_resources[0][0];
    mobj_t *second = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(8.5, 55.5), 0), MT_FG_HOVER_FREIGHTER);
    if (!second || !P_HarvestUnitTo(&level, second, pit)) return fail("second transporter order");
    second->harvest.resource_type = 0;
    second->harvest.cargo = 750;
    bool second_delivered = false;
    for (int t = 0; t < 30 * 90 && !second_delivered; ++t) {
        int cargo = second->harvest.cargo;
        if (!rts_tick(model, NULL)) return fail("tick shared dock");
        if (P_HarvesterDocked(first) && first->harvest.phase == HARVEST_PHASE_UNLOADING &&
            !fixed2_near(fixed3_xy(first->core.position), FIXED2_LIT(7.5, 55.5), FIXED_LIT(0.001)))
            return fail("waiting transporter must not push a docked transporter away");
        if (second->harvest.cargo < cargo) second_delivered = true;
    }
    if (!second_delivered || level.player_resources[0][0] < stock + 750 + 270)
        return fail("both transporters deliver sequentially through the shared bay");
    int cargo = second->harvest.cargo;
    stock = level.player_resources[0][0];
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {second->id}};
    G_RunTiccmd(0, &stop);
    P_RemoveMobj(first);
    for (int t = 0; t < 90; ++t)
        if (!rts_tick(model, NULL)) return fail("tick interrupted unload");
    if (second->harvest.cargo != cargo || level.player_resources[0][0] != stock)
        return fail("stop interrupts unloading without crediting or losing cargo");
    int source_amount = level.resource_vents[target].amount;
    if (!P_HarvestUnitTo(&level, second, pit)) return fail("resume interrupted unload");
    for (int t = 0; t < 30 * 90 && second->harvest.cargo; ++t)
        if (!rts_tick(model, NULL)) return fail("tick resumed unload");
    int gathered = source_amount - level.resource_vents[target].amount;
    if (second->harvest.cargo || level.player_resources[0][0] != stock + cargo + gathered)
        return fail("resumed harvest delivers retained cargo without loss");
    puts("PASS: shared bay stays occupied during unloading; stop and resume preserve cargo");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_dark_reign_transport_and_flight_traits());
    RTS_RUN(test_gather_attach_animate_and_build());
    RTS_RUN(test_taelon_delivery_uses_power_generator());
    RTS_RUN(test_all_transporter_deliveries());
    RTS_RUN(test_delivery_requires_live_accessible_bay());
    RTS_RUN(test_shared_bay_and_interrupted_unload());
    return 0;
}
