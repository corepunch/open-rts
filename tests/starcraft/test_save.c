#include "t_local.h"
#include "sc_local.h"
#include <limits.h>
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft saves", #c)

typedef struct {
    char magic[8];
    uint32_t version, header_size, object_size, body_size, checksum, signature;
    saveinfo_t info;
    int width, height, time, speed, vents, objects;
    bool paused;
    fvec2_t camera;
    daylight_t daylight;
    int resources[8][RTS_MAX_RESOURCES];
    uint8_t random;
    uint32_t vision[8], next_id, next_order;
    uint16_t income_scale[8];
    uint8_t upgrades[sizeof(level.upgrades)];
    outcome_t outcome;
    uint8_t sides, foes;
    sidestats_t side_stats[8];
    size_t extra_size;
    AiContext ai;
    hudtext_t hud;
} test_save_header_t;

typedef struct {
    mobj_t object;
    uint32_t target, attack, base;
    bool production, removed;
    production_t queue;
} test_savedmobj_t;

static uint32_t save_test_checksum(const void *data, size_t length) {
    const uint8_t *bytes=data;
    uint32_t hash=UINT32_C(2166136261);
    for(size_t i=0;i<length;i++)hash=(hash^bytes[i])*UINT32_C(16777619);
    return hash;
}

enum { BAD_QUEUE_DEPTH, BAD_TIME_LEFT, BAD_TIME_TOTAL, BAD_CELL, BAD_PRODUCT };

static bool reject_malformed_construction(const void *data,size_t length,uint32_t site_id,int defect,
                                           app_t *app,AiContext *ai,hudtext_t *hud) {
    uint8_t *copy=malloc(length);
    if(!copy)return false;
    memcpy(copy,data,length);
    test_save_header_t *header=(test_save_header_t *)copy;
    if(header->header_size!=sizeof(*header)||header->object_size!=sizeof(test_savedmobj_t)) {
        free(copy);
        return false;
    }
    size_t cells=(size_t)header->width*header->height;
    size_t offset=sizeof(*header)+cells*5+(size_t)header->vents*sizeof(resourcevent_t);
    int site_index=-1;
    test_savedmobj_t site;
    for(int i=0;i<header->objects;i++) {
        test_savedmobj_t record;
        memcpy(&record,copy+offset+(size_t)i*sizeof(record),sizeof(record));
        if(record.object.id==site_id) { site_index=i; site=record; break; }
    }
    if(site_index<0||!site.production) { free(copy); return false; }
    switch(defect) {
        case BAD_QUEUE_DEPTH:
            site.queue.pending[0]=(productionitem_t){site.queue.actor_id,
                site.queue.product_class,site.queue.product_type,site.queue.time_ms};
            site.queue.queue_count=2;
            break;
        case BAD_TIME_LEFT: site.queue.time_left_ms=INT_MIN; break;
        case BAD_TIME_TOTAL: site.queue.time_ms=INT_MAX; break;
        case BAD_CELL: site.queue.cell.x=header->width; break;
        case BAD_PRODUCT: site.queue.product_type=INT_MAX; break;
    }
    memcpy(copy+offset+(size_t)site_index*sizeof(site),&site,sizeof(site));
    header->checksum=0;
    header->checksum=save_test_checksum(header,sizeof(*header)) ^
        save_test_checksum(copy+sizeof(*header),header->body_size);
    bool rejected=!G_LoadGameBlob(copy,length,app,ai,hud);
    free(copy);
    return rejected;
}

static bool reject_malformed_queue_duration(const void *data,size_t length,uint32_t object_id,
                                             bool pending,app_t *app,AiContext *ai,hudtext_t *hud) {
    uint8_t *copy=malloc(length);
    if(!copy)return false;
    memcpy(copy,data,length);
    test_save_header_t *header=(test_save_header_t *)copy;
    if(header->header_size!=sizeof(*header)||header->object_size!=sizeof(test_savedmobj_t)) {
        free(copy);
        return false;
    }
    size_t cells=(size_t)header->width*header->height;
    size_t offset=sizeof(*header)+cells*5+(size_t)header->vents*sizeof(resourcevent_t);
    int found=-1;
    test_savedmobj_t record;
    for(int i=0;i<header->objects;i++) {
        memcpy(&record,copy+offset+(size_t)i*sizeof(record),sizeof(record));
        if(record.object.id==object_id) { found=i; break; }
    }
    if(found<0||!record.production|| (pending&&record.queue.queue_count<2)) {
        free(copy);
        return false;
    }
    if(pending) record.queue.pending[0].time_ms=INT_MAX;
    else record.queue.time_ms=INT_MAX;
    memcpy(copy+offset+(size_t)found*sizeof(record),&record,sizeof(record));
    header->checksum=0;
    header->checksum=save_test_checksum(header,sizeof(*header)) ^
        save_test_checksum(copy+sizeof(*header),header->body_size);
    bool rejected=!G_LoadGameBlob(copy,length,app,ai,hud);
    free(copy);
    return rejected;
}

static bool accepts_retry_or_release_state(const void *data,size_t length,uint32_t object_id,
                                          bool release,app_t *app,AiContext *ai,hudtext_t *hud) {
    uint8_t *copy=malloc(length);
    if(!copy)return false;
    memcpy(copy,data,length);
    test_save_header_t *header=(test_save_header_t *)copy;
    if(header->header_size!=sizeof(*header)||header->object_size!=sizeof(test_savedmobj_t)) {
        free(copy);
        return false;
    }
    size_t cells=(size_t)header->width*header->height;
    size_t offset=sizeof(*header)+cells*5+(size_t)header->vents*sizeof(resourcevent_t);
    int found=-1;
    test_savedmobj_t record;
    for(int i=0;i<header->objects;i++) {
        memcpy(&record,copy+offset+(size_t)i*sizeof(record),sizeof(record));
        if(record.object.id==object_id) { found=i; break; }
    }
    if(found<0||!record.production) { free(copy); return false; }
    if(release) {
        record.queue.release_active=true;
        record.queue.release_ready=false;
        record.queue.time_left_ms=0;
    } else {
        record.queue.blocked=true;
        record.queue.time_left_ms=250;
    }
    memcpy(copy+offset+(size_t)found*sizeof(record),&record,sizeof(record));
    header->checksum=0;
    header->checksum=save_test_checksum(header,sizeof(*header)) ^
        save_test_checksum(copy+sizeof(*header),header->body_size);
    bool loaded=G_LoadGameBlob(copy,length,app,ai,hud);
    if(loaded) {
        mobj_t *unit=P_MobjById(object_id);
        loaded=unit&&unit->production&&unit->production->queue_count==2&&
            (release?(unit->production->release_active&&!unit->production->release_ready&&
                       unit->production->time_left_ms==0):
                     (unit->production->blocked&&unit->production->time_left_ms==250));
    }
    free(copy);
    return loaded;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    CHECK(G_SaveLayout() == 22);
    sc_set_map_mode(SC_MAP_CAMPAIGN);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    sc_mission_t *m = level.mission;
    CHECK(m && m->trig_count > 0 && m->research_rules_loaded &&
          m->map_mode==SC_MAP_CAMPAIGN && m->upgrade_max_level[0][0]==3 &&
          m->tech_available[0][0]);
    uint32_t mode_hash=G_ConsistencyExtra(0);
    m->map_mode=SC_MAP_MELEE;
    CHECK(G_ConsistencyExtra(0)!=mode_hash);
    m->map_mode=SC_MAP_CAMPAIGN;
    m->rng=1;
    uint32_t random_hash=G_ConsistencyExtra(0);
    CHECK(sc_random()==346 && G_ConsistencyExtra(0)!=random_hash);
    uint32_t outcome_hash = G_ConsistencyExtra(0);
    m->player_result[7] = 2;
    CHECK(G_ConsistencyExtra(0) != outcome_hash);
    uint32_t custom_score_hash = G_ConsistencyExtra(0);
    m->custom_score[1] = 123456;
    CHECK(G_ConsistencyExtra(0) != custom_score_hash);
    uint32_t inactive_score_hash = G_ConsistencyExtra(0);
    m->score_variables[2][4] = 654321;
    CHECK(G_ConsistencyExtra(0) != inactive_score_hash);
    uint32_t research_rules_hash = G_ConsistencyExtra(0);
    m->upgrade_max_level[0][7] = 2;
    m->tech_available[2][SC_TECH_SIEGE_MODE] = 0;
    CHECK(G_ConsistencyExtra(0) != research_rules_hash);
    m->switches[12] = 1;
    m->deaths[1][0] = UINT32_C(0x80000000);
    m->elapsed_ms = 12345;
    m->countdown_ms = 9876;
    m->ai_on[4] = false;
    m->rt[0] = (sc_trig_t){.cursor = 3, .wait_ms = 1200, .current = 1, .running = true};
    mobj_t *queued_unit=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(4.5,4.5),0),MT_MARINE);
    CHECK(queued_unit);queued_unit->owner=queued_unit->team=0;
    ticcmd_t move={.order=TC_MOVE,.count=1,.units={queued_unit->id},.position=fixed3_from_fixed2(FIXED2_LIT(9.5,4.5),0)};
    G_RunTiccmd(0,&move);move.queued=true;move.position=fixed3_from_fixed2(FIXED2_LIT(12.5,4.5),0);
    G_RunTiccmd(0,&move);CHECK(queued_unit->sc.queue.count==1);
    const StaticProductDefinition *marine_product=G_ModelProductByClassType(NULL,
        RTS_PRODUCT_UNIT,MT_MARINE);
    CHECK(marine_product);
    int marine_time=G_ModelProductTrainingTimeMs(marine_product);
    queued_unit->production=calloc(1,sizeof(*queued_unit->production));
    CHECK(queued_unit->production);
    *queued_unit->production=(production_t){.actor_id=MT_MARINE,
        .product_class=RTS_PRODUCT_UNIT,.product_type=MT_MARINE,.queue_count=2,
        .time_ms=marine_time,.time_left_ms=marine_time,.pending={{MT_MARINE,
            RTS_PRODUCT_UNIT,MT_MARINE,marine_time}}};
    mobj_t *attacker=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(5.5,5.5),0),MT_MARINE);
    mobj_t *target=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(7.5,5.5),0),MT_MARINE);
    CHECK(attacker && target);
    attacker->owner=attacker->team=0; attacker->allegiance=ALLEGIANCE_PLAYER;
    target->owner=target->team=1; target->allegiance=ALLEGIANCE_ENEMY;
    ticcmd_t first={.order=TC_MOVE,.count=1,.units={attacker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(6.5,5.5),0)};
    G_RunTiccmd(0,&first);
    ticcmd_t attack={.order=TC_ATTACK,.queued=true,.count=1,.units={attacker->id},
        .target=target->id,.position=target->core.position};
    G_RunTiccmd(0,&attack); CHECK(attacker->sc.queue.count==1);
    target->owner=target->team=0; target->allegiance=ALLEGIANCE_PLAYER;
    P_ClearMove(attacker); attacker->movement.order_arrived=true;
    P_Ticker();
    CHECK(attacker->sc.queue.active==TC_ATTACK && attacker->sc.queue.forced &&
          attacker->attack.target==target && P_CanTarget(attacker,target));
    uint32_t attacker_id=attacker->id, target_id=target->id;
    mobj_t *geyser = sc_spawn_actor(MT_VESPENE_GEYSER - 1, (ivec2_t){320, 320}, 11);
    CHECK(geyser && sc_add_resource_vent(geyser, 5000));
    mobj_t *saved_carrier = sc_spawn_actor(MT_CARRIER - 1, (ivec2_t){384, 384}, 11);
    mobj_t *saved_fighter = sc_spawn_actor(MT_INTERCEPTOR - 1, (ivec2_t){384, 384}, 11);
    CHECK(saved_carrier && saved_fighter); sc_initial_hangar(saved_carrier, 3); saved_fighter->sc.parent = saved_carrier->id;
    sc_initial_vitals(saved_carrier, 4, 33, 0);
    saved_carrier->sc.timers[SC_TIMER_MAIN_ORDER] = 15;
    mobj_t *saved_ghost = sc_spawn_actor(MT_GHOST - 1, (ivec2_t){448, 384}, 0); CHECK(saved_ghost);
    level.upgrades[21][0].weapon = 1; sc_initial_vitals(saved_ghost, 8, 0, 33);
    sc_initial_hp(saved_ghost, 1);
    sc_initial_hallucination(saved_ghost);
    saved_ghost->sc.flags |= SC_INVINCIBLE;
    uint32_t ghost_id = saved_ghost->id;
    mobj_t *saved_duke = sc_spawn_actor(MT_DUKE_TANK - 1, (ivec2_t){512, 384}, 0); CHECK(saved_duke);
    mobj_t *saved_neutral = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){576, 384}, 1); CHECK(saved_neutral);
    saved_neutral->owner = saved_neutral->team = 11;
    uint32_t neutral_id = saved_neutral->id;
    uint32_t sprite_owner_hash = G_Consistency();
    saved_neutral->sc.sprite_owner = 2;
    CHECK(G_Consistency() != sprite_owner_hash);
    saved_neutral->sc.sprite_owner = 1;
    ticcmd_t duke_deploy = {.order = TC_DEPLOY, .count = 1, .units = {saved_duke->id}};
    G_RunTiccmd(0, &duke_deploy); CHECK(saved_duke->core.state_id == SC_DUKE_SIEGE_STATE);
    uint32_t duke_id = saved_duke->id;
    uint32_t carrier_id = saved_carrier->id;
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 1;
    level.sight.allies[0] &= ~(UINT32_C(0x40000000) >> 2); /* revoked sharing */
    m->loc_count = SC_LOCATION_COUNT;
    m->loc_left[SC_LOCATION_COUNT - 1] = 17;
    m->loc_top[SC_LOCATION_COUNT - 1] = 29;
    m->loc_right[SC_LOCATION_COUNT - 1] = 41;
    m->loc_bottom[SC_LOCATION_COUNT - 1] = 53;
    m->loc_elevation[SC_LOCATION_COUNT - 1] = 0x35;
    uint32_t location_hash = G_ConsistencyExtra(0);
    m->loc_right[SC_LOCATION_COUNT - 1]++;
    CHECK(G_ConsistencyExtra(0) != location_hash);
    m->loc_right[SC_LOCATION_COUNT - 1]--;
    uint32_t geyser_id = geyser->id;
    int geyser_vent = level.resource_vent_count - 1;
    uint32_t queued_id=queued_unit->id, queued_hash=G_Consistency();
    mobj_t *foundation=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5,20.5),0),MT_BARRACKS);
    mobj_t *builder=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(19.5,20.5),0),MT_SCV);
    const StaticProductDefinition *barracks=G_ModelProductByClassType(NULL,
        RTS_PRODUCT_BUILDING,MT_BARRACKS);
    CHECK(foundation&&builder&&barracks);
    foundation->owner=foundation->team=builder->owner=builder->team=0;
    foundation->target=builder;
    builder->target=foundation;
    builder->sc.queue=(sc_order_queue_t){.active=TC_CONSTRUCT,.owner=0,
        .target=foundation->id};
    foundation->production=calloc(1,sizeof(*foundation->production));
    CHECK(foundation->production);
    int barracks_time=G_ModelProductTrainingTimeMs(barracks);
    *foundation->production=(production_t){.actor_id=MT_BARRACKS,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_BARRACKS,.queue_count=1,
        .time_ms=barracks_time,.time_left_ms=barracks_time-barracks_time/4,
        .placed=true,.cell={20,20}};
    foundation->sc.construction=(sc_construction_t){.worker_hp=builder->hp,
        .frames=barracks_time*3/4*24/1000,.stage=1};
    uint32_t foundation_id=foundation->id, foundation_builder_id=builder->id;
    mobj_t *pending_builder=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(30.5,20.5),0),MT_SCV);
    CHECK(pending_builder);
    pending_builder->owner=pending_builder->team=0;
    pending_builder->production=calloc(1,sizeof(*pending_builder->production));
    CHECK(pending_builder->production);
    *pending_builder->production=(production_t){.actor_id=MT_BARRACKS,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_BARRACKS,.queue_count=1,
        .time_ms=barracks_time,.time_left_ms=barracks_time,.placed=true,.cell={31,20}};
    uint32_t pending_builder_id=pending_builder->id;
    uint32_t construction_hash=G_Consistency();
    foundation->sc.construction.worker_hp++;
    CHECK(G_Consistency()!=construction_hash);
    foundation->sc.construction.worker_hp--;
    construction_hash=G_Consistency();
    foundation->sc.construction.frames++;
    CHECK(G_Consistency()!=construction_hash);
    foundation->sc.construction.frames--;
    construction_hash=G_Consistency();
    foundation->sc.construction.stage++;
    CHECK(G_Consistency()!=construction_hash);
    foundation->sc.construction.stage--;
    queued_hash=G_Consistency();
    saved_carrier->sc.timers[SC_TIMER_MAIN_ORDER] = 14;
    CHECK(G_Consistency() != queued_hash);
    saved_carrier->sc.timers[SC_TIMER_MAIN_ORDER] = 15;
    queued_hash = G_Consistency();
    snprintf(m->objectives, sizeof(m->objectives), "Saved objective");
    uint32_t presentation_hash = G_ConsistencyExtra(0);
    m->unit_speech_muted = true;
    CHECK(G_ConsistencyExtra(0) == presentation_hash); /* Local audio preference is not lockstep state. */
    app_t app = {0};
    AiContext ai = {0};
    hudtext_t hud = {0};
    void *saved = NULL;
    size_t length = 0;
    CHECK(G_SaveGameBlob(&saved, &length, "mission", &app, &ai, &hud));
    for(int defect=BAD_QUEUE_DEPTH;defect<=BAD_PRODUCT;defect++) {
        uint32_t before=G_Consistency();
        CHECK(reject_malformed_construction(saved,length,foundation_id,defect,&app,&ai,&hud));
        CHECK(G_Consistency()==before);
    }
    uint32_t before_bad_duration=G_Consistency();
    CHECK(reject_malformed_queue_duration(saved,length,queued_id,false,&app,&ai,&hud));
    CHECK(G_Consistency()==before_bad_duration);
    CHECK(reject_malformed_queue_duration(saved,length,queued_id,true,&app,&ai,&hud));
    CHECK(G_Consistency()==before_bad_duration);
    CHECK(accepts_retry_or_release_state(saved,length,queued_id,false,&app,&ai,&hud));
    CHECK(G_LoadGameBlob(saved,length,&app,&ai,&hud));
    CHECK(accepts_retry_or_release_state(saved,length,queued_id,true,&app,&ai,&hud));
    CHECK(G_LoadGameBlob(saved,length,&app,&ai,&hud));
    /* Reload the map as the driver does, so no saved pointer can accidentally
     * stand in for the new level's CHK allocation or trigger runtime. */
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_MELEE);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    m = level.mission;
    const uint8_t *strings = m->str;
    blob_t file = m->file;
    const uint8_t *triggers = m->trig, *properties = m->uprp;
    sc_trig_t *runtime = m->rt;
    sc_terrain_t *terrain=m->terrain;
    CHECK(terrain);
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 2;
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    queued_unit=P_MobjById(queued_id);
    CHECK(queued_unit && queued_unit->sc.queue.count==1 && queued_unit->sc.queue.pending[0].position.x==FIXED_LIT(12.5)&&
          queued_unit->production&&queued_unit->production->queue_count==2&&
          queued_unit->production->time_ms==marine_time&&
          queued_unit->production->pending[0].time_ms==marine_time);
    foundation=P_MobjById(foundation_id);
    CHECK(foundation&&foundation->type_id==MT_BARRACKS&&foundation->target&&
          foundation->target->id==foundation_builder_id&&foundation->target->target==foundation&&
          foundation->target->sc.queue.active==TC_CONSTRUCT&&
          foundation->target->sc.queue.target==foundation_id&&foundation->production&&
          foundation->production->time_left_ms==barracks_time-barracks_time/4&&
          foundation->production->cell.x==20&&
          foundation->sc.construction.worker_hp==foundation->target->hp);
    pending_builder=P_MobjById(pending_builder_id);
    CHECK(pending_builder&&pending_builder->type_id==MT_SCV&&pending_builder->production&&
          pending_builder->production->placed&&!pending_builder->target&&
          pending_builder->production->cell.x==31);
    attacker=P_MobjById(attacker_id); target=P_MobjById(target_id);
    CHECK(attacker && target && attacker->sc.queue.active==TC_ATTACK && attacker->sc.queue.forced &&
          attacker->sc.queue.target_owner==target->owner && attacker->attack.target==target &&
          P_CanTarget(attacker,target));
    geyser = P_MobjById(geyser_id);
    CHECK(geyser && geyser->owner == 11 && geyser->type_id == MT_VESPENE_GEYSER);
    CHECK(geyser_vent < level.resource_vent_count && level.resource_vents[geyser_vent].source_id == geyser_id &&
          level.resource_vents[geyser_vent].amount == 5000 && !level.resource_vents[geyser_vent].active);
    CHECK(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1));
    saved_carrier = P_MobjById(carrier_id);
    CHECK(saved_carrier && saved_carrier->owner == 11 && saved_carrier->sc.hangar == 3 && sc_hangar_count(saved_carrier) == 4 &&
          saved_carrier->sc.timers[SC_TIMER_MAIN_ORDER] == 15);
    CHECK(saved_carrier->sc.shields == 49 * 256);
    saved_ghost = P_MobjById(ghost_id);
    CHECK(saved_ghost && saved_ghost->sc.energy == 82 * 256 + 128 &&
          saved_ghost->hp == 1 && saved_ghost->sc.wound == 141 && (saved_ghost->sc.flags & SC_INVINCIBLE) &&
          (saved_ghost->sc.flags & SC_HALLUCINATION) && saved_ghost->sc.timers[SC_TIMER_LIFE] == 1350);
    saved_duke = P_MobjById(duke_id);
    CHECK(saved_duke && saved_duke->core.state_id == SC_DUKE_SIEGE_STATE);
    saved_neutral = P_MobjById(neutral_id);
    CHECK(saved_neutral && saved_neutral->owner == 11 && saved_neutral->team == 11 &&
          saved_neutral->sc.sprite_owner == 1);
    CHECK(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 2)));
    CHECK(G_Consistency()==queued_hash);
    /* Construction counts 24 Hz frames: five tics advance four of them. */
    for(int i=0;i<5;i++) { leveltime++;G_ProductionTicker(RTS_TICK_MS); }
    CHECK(foundation->production&&foundation->sc.construction.frames==
          barracks_time*3/4*24/1000-4&&foundation->sc.construction.stage==1&&
          foundation->production->time_left_ms==
          (foundation->sc.construction.frames*1000+23)/24);
    CHECK(m->str == strings && m->switches[12] == 1 && m->deaths[1][0] == UINT32_C(0x80000000));
    CHECK(m->file.bytes == file.bytes && m->trig == triggers && m->uprp == properties && m->rt == runtime);
    CHECK(m->loc_count == SC_LOCATION_COUNT && m->loc_left[SC_LOCATION_COUNT - 1] == 17 &&
          m->loc_top[SC_LOCATION_COUNT - 1] == 29 && m->loc_right[SC_LOCATION_COUNT - 1] == 41 &&
          m->loc_bottom[SC_LOCATION_COUNT - 1] == 53 && m->loc_elevation[SC_LOCATION_COUNT - 1] == 0x35);
    CHECK(m->terrain==terrain && m->rng==22695478u);
    CHECK(m->player_result[7] == 2 && m->custom_score[1] == 123456 &&
          m->score_variables[2][4] == 654321);
    CHECK(m->unit_speech_muted); /* Ordinary save/load restores the local trigger state. */
    CHECK(m->map_mode==SC_MAP_CAMPAIGN&&sc_current_map_mode()==SC_MAP_CAMPAIGN&&
          m->research_rules_loaded && m->upgrade_max_level[0][7] == 2 &&
          !m->tech_available[2][SC_TECH_SIEGE_MODE]);
    uint32_t slots_hash=G_ConsistencyExtra(0);
    m->custom_slots_set=1; m->custom_slot_kind[1]=SC_SLOT_HUMAN;
    m->custom_slot_race[1]=2;
    CHECK(G_ConsistencyExtra(0)!=slots_hash);
    m->custom_slots_set=0; m->custom_slot_kind[1]=0; m->custom_slot_race[1]=0;
    CHECK(m->elapsed_ms == 12345 && m->countdown_ms == 9876 && !m->ai_on[4]);
    CHECK(m->rt[0].cursor == 3 && m->rt[0].wait_ms == 1200 && m->rt[0].running);
    int target_hp=target->hp;
    for (int i = 0; i < RTS_TICRATE * 3; i++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
    CHECK(target->hp<target_hp);
    CHECK(saved_duke->type_id == MT_DUKE_SIEGE);
    CHECK(!strcmp(m->objectives, "Saved objective"));
    free(saved);
    size_t extra_size = G_SaveExtraSize();
    saved = malloc(extra_size);
    CHECK(saved);
    G_SaveExtra(saved);
    ((sc_mission_t *)saved)->trig_count++;
    CHECK(!G_LoadExtra(saved, extra_size) && m->elapsed_ms == 12345);
    CHECK(!G_LoadExtra(saved, extra_size - 1));
    G_SaveExtra(saved);
    sc_trig_t *bad = (sc_trig_t *)((uint8_t *)saved + extra_size - (size_t)m->trig_count * 8 * sizeof(*bad));
    bad->cursor = 65;
    CHECK(!G_LoadExtra(saved, extra_size) && m->rt[0].cursor == 3);
    bad->cursor = 3; bad->current = 8;
    CHECK(!G_LoadExtra(saved, extra_size) && m->rt[0].current == 1);
    free(saved);
    P_FreeLevel(&level);
    /* A fresh session has no custom-lobby state. Restore its player before
     * constructing the HUD, whose dialog art is selected by player race. */
    const char *map = "data/STARCRAFT/maps/(2)road war.scm/staredit/scenario.chk";
    int kinds[8] = {SC_SLOT_COMPUTER, SC_SLOT_HUMAN, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    int races[8] = {0, 2};
    sc_set_map_mode(SC_MAP_CAMPAIGN); sc_set_custom_slots(NULL,NULL);
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    int base_player=consoleplayer,base_side=sc_player_side(base_player);
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_MELEE);
    sc_set_custom_slots(kinds, races);
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    int player = consoleplayer;
    CHECK(sc_player_side(player) == 2);
    sc_mission_t *custom_session=level.mission;
    CHECK(custom_session->custom_slots_set && custom_session->custom_slot_kind[1]==SC_SLOT_HUMAN &&
          custom_session->custom_slot_race[1]==2);
    uint32_t custom_session_hash=G_ConsistencyExtra(0);
    custom_session->custom_slot_race[1]=1;
    CHECK(G_ConsistencyExtra(0)!=custom_session_hash);
    custom_session->custom_slot_race[1]=2;
    int other = player == 0 ? 1 : 0;
    uint32_t pbit = UINT32_C(0x40000000) >> player, obit = UINT32_C(0x40000000) >> other;
    ticcmd_t ally = {.order=TC_ALLY,.target=other,.product=1};
    ticcmd_t vision = {.order=TC_SHARE_SIGHT,.target=other,.product=1};
    uint32_t hash = G_ConsistencyExtra(0);
    CHECK((G_AllianceMasks(&level)[player]&pbit) && (level.sight.allies[player]&pbit));
    netactive=true;
    CHECK(G_QueueTiccmd(&ally));
    CHECK(!(G_AllianceMasks(&level)[player]&obit));
    ticcmd_t queued; G_BuildTiccmd(&queued); G_RunTiccmd(player,&queued);
    netactive=false;
    G_RunTiccmd(player,&vision);
    CHECK((G_AllianceMasks(&level)[player]&obit) && !(G_AllianceMasks(&level)[other]&pbit));
    CHECK((level.sight.allies[other]&pbit) && !(level.sight.allies[player]&obit));
    mobj_t self = {.owner=player,.team=player,.allegiance=ALLEGIANCE_PLAYER};
    mobj_t foe = {.owner=other,.team=other,.allegiance=ALLEGIANCE_ENEMY};
    CHECK(P_IsAlly(&self,&foe) && !P_IsAlly(&foe,&self));
    foe.core.position.x=FIXED_ONE;
    level.sight.cells[0]=pbit; level.sight.cells[1]=obit;
    CHECK(P_VisibleTo(&foe,&self) && !P_VisibleTo(&self,&foe));
    CHECK(G_ConsistencyExtra(0) != hash);
    ally.target=player; G_RunTiccmd(player,&ally);
    ally.target=other; ally.product=2; G_RunTiccmd(player,&ally);
    ally.target=7; ally.product=1; G_RunTiccmd(player,&ally);
    CHECK(G_AllianceMasks(&level)[player]&obit);
    CHECK(!(G_AllianceMasks(&level)[player] & (UINT32_C(0x40000000)>>7)));
    sc_mission_t *alliance_mission = level.mission;
    alliance_mission->allied_victory[player] |= obit;
    uint32_t victory_hash = G_ConsistencyExtra(0);
    ally.target=other; ally.product=0; G_RunTiccmd(player,&ally);
    CHECK(!(alliance_mission->allies[player]&obit) && !(alliance_mission->allied_victory[player]&obit));
    ally.product=1; G_RunTiccmd(player,&ally);
    CHECK((alliance_mission->allies[player]&obit) && !(alliance_mission->allied_victory[player]&obit));
    CHECK(G_ConsistencyExtra(0) != victory_hash);
    alliance_mission->allied_victory[player] |= obit;
    alliance_mission->neutral_relations[player] |= obit;
    uint32_t neutral_hash = G_ConsistencyExtra(0);
    alliance_mission->neutral_relations[player] &= ~obit;
    CHECK(G_ConsistencyExtra(0) != neutral_hash);
    alliance_mission->neutral_relations[player] |= obit;
    CHECK(G_SaveGameBlob(&saved, &length, "custom", &app, &ai, &hud));
    P_FreeLevel(&level);
    sc_set_custom_slots(NULL, NULL);
    consoleplayer = 0;
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    CHECK(consoleplayer != player || sc_player_side(consoleplayer) != 2);
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    CHECK(consoleplayer == player && sc_player_side(consoleplayer) == 2);
    CHECK(((sc_mission_t *)level.mission)->allied_victory[player]&obit);
    CHECK(((sc_mission_t *)level.mission)->neutral_relations[player]&obit);
    CHECK((G_AllianceMasks(&level)[player]&obit) && !(G_AllianceMasks(&level)[other]&pbit));
    CHECK((level.sight.allies[other]&pbit) && !(level.sight.allies[player]&obit));
    size_t custom_extra_size = G_SaveExtraSize();
    ((sc_mission_t *)level.mission)->result = 1;
    void *extra = malloc(custom_extra_size); CHECK(extra); G_SaveExtra(extra);
    ((sc_mission_t *)level.mission)->result = 2;
    netgame = true; consoleplayer = (player + 1) % 8;
    CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == (player + 1) % 8);
    CHECK(((sc_mission_t *)level.mission)->result == 1); /* Ordinary load restores its result. */
    netgame = false; CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == player);
    free(extra);
    /* A resync carries shared player outcomes, not the authority's local UI. */
    alliance_mission = level.mission;
    alliance_mission->player_result[player] = 1;
    alliance_mission->player_result[other] = 2;
    alliance_mission->unit_speech_muted = true; /* Sender's presentation state is not authoritative. */
    alliance_mission->result = 1; level.outcome = OUTCOME_VICTORY; level.foes = 0x80;
    void *resync = NULL; size_t resync_size = 0;
    CHECK(G_ResyncSave(&resync, &resync_size, &app, &ai, &hud));
    consoleplayer = other; netgame = true;
    alliance_mission->unit_speech_muted = false;
    alliance_mission->result = 0; level.outcome = OUTCOME_NONE; level.foes = 0x02;
    CHECK(G_ResyncLoad(resync, resync_size, &app, &ai, &hud));
    free(resync);
    alliance_mission = level.mission;
    CHECK(consoleplayer == other && !alliance_mission->result && level.outcome == OUTCOME_NONE && level.foes == 0x02 &&
          alliance_mission->player_result[player] == 1 && alliance_mission->player_result[other] == 2 &&
          !alliance_mission->unit_speech_muted);
    sc_mission_tick(&level, &hud, RTS_TICK_MS);
    CHECK(alliance_mission->result == 2 && level.outcome == OUTCOME_DEFEAT);
    /* Loading a custom Melee save restores its seat/race selection for a later
     * Restart Mission map decode. A new session setter still clears it. */
    netgame=false;
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_CAMPAIGN);
    CHECK(G_DoLoadLevel(map,&level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path)>0&&P_InitSight());
    CHECK(G_LoadGameBlob(saved,length,&app,&ai,&hud));
    alliance_mission=level.mission;
    CHECK(sc_current_map_mode()==SC_MAP_MELEE&&alliance_mission->custom_slots_set&&
          alliance_mission->custom_slot_kind[1]==SC_SLOT_HUMAN&&
          alliance_mission->custom_slot_race[1]==2);
    P_FreeLevel(&level);
    CHECK(G_DoLoadLevel(map,&level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path)>0&&P_InitSight());
    CHECK(consoleplayer==player&&sc_player_side(consoleplayer)==2&&
          ((sc_mission_t *)level.mission)->custom_slots_set);
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_CAMPAIGN);
    CHECK(G_DoLoadLevel(map,&level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path)>0&&P_InitSight());
    CHECK(!((sc_mission_t *)level.mission)->custom_slots_set&&
          consoleplayer==base_player&&sc_player_side(consoleplayer)==base_side);
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_MELEE); sc_set_custom_slots(kinds,races);
    CHECK(G_DoLoadLevel(map,&level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path)>0&&P_InitSight());
    netgame = false; consoleplayer = player; menuleave = false;
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    CHECK(M_Init(&app, "data/STARCRAFT"));
    menu_t *bar = G_InitHUD(&app, "data/STARCRAFT"); CHECK(bar);
    menuitem_t *f10 = NULL;
    for (int i = 0; i < bar->numitems; ++i) if (bar->items[i].hotkey == SDLK_F10) f10 = &bar->items[i];
    blob_t art = {0}; spritesheet_t expected = {0}; uint32_t palette[256] = {0};
    CHECK(f10 && f10->sheet && sc_read("data/STARCRAFT", "dlgs/protoss.grp", &art));
    CHECK(sc_decode_grp(&art, palette, false, &expected));
    CHECK(f10->sheet->numlumps == expected.numlumps);
    size_t pixels = (size_t)expected.cells[1].rect.w * expected.cells[1].rect.h;
    CHECK(!memcmp(f10->sheet->lumps[1].indices, expected.lumps[1].indices, pixels));
    R_FreeSprite(&expected); W_FreeFile(&art); G_ShutdownHUD(); M_Shutdown(); SDL_Quit();
    free(saved); P_FreeLevel(&level);
    puts("PASS: StarCraft mission save round trip and malformed runtime rejection");
    return 0;
}
