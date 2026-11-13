#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"Z native terrain speed",#c)

int main(void) {
    RtsGameModel *model=rts_game_model_create();
    RtsGameModelConfig config={.data_root="data/Z"};
    CHECK(model&&rts_game_model_load(model,&config));
    zmap_t *map=level.native_data;
    int houses=0; ivec2_t house_cell={0};
    for (thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        if (th->function!=P_MobjThinker || (mo->type_id!=MT_DESERT_HOUSE && mo->type_id!=MT_JUNGLE_HOUSE &&
            mo->type_id!=MT_VOLCANO_HOUSE && mo->type_id!=MT_CITY_HOUSE && mo->type_id!=MT_ARCTIC_HOUSE)) continue;
        isize2_t size; CHECK(Z_SpriteSize(map,states[mo->core.state_id].frame,&size));
        ivec2_t centre=fixed2_cell(fixed2_scale(fixed3_xy(mo->core.position),FIXED_FROM_INT(Z_TILE)));
        ivec2_t origin=ivec2_sub(centre,(ivec2_t){size.w/2,size.h/2});
        ivec2_t cell={origin.x/Z_TILE,origin.y/Z_TILE};
        CHECK(L_Contains(&level,cell.x,cell.y));
        CHECK((map->ground_attr[cell.y*Z_MAP_STRIDE+cell.x]&0x7f)==0x11);
        house_cell=cell; houses++;
    }
    CHECK(houses>0);
    uint8_t saved=map->ground_attr[house_cell.y*Z_MAP_STRIDE+house_cell.x];
    uint32_t hash=G_Consistency(); app_t app={0}; hudtext_t hud={0}; void *blob=NULL; size_t bytes=0;
    CHECK(G_SaveGameBlob(&blob,&bytes,"House terrain",&app,rts_game_model_ai(model),&hud));
    map->ground_attr[house_cell.y*Z_MAP_STRIDE+house_cell.x]=0;
    Z_RebuildCliffs(&level);
    CHECK(G_LoadGameBlob(blob,bytes,&app,rts_game_model_ai(model),&hud)); free(blob);
    map=level.native_data;
    CHECK(map->ground_attr[house_cell.y*Z_MAP_STRIDE+house_cell.x]==saved);
    CHECK(G_Consistency()==hash);
    Z_RebuildCliffs(&level);
    CHECK((map->terrain[house_cell.y*Z_MAP_STRIDE+house_cell.x]&0x7f)==0x11);
    mobj_t *robot=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(6.5,6.5),0),MT_UNIT_GRUNT);
    mobj_t *vehicle=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(10.5,10.5),0),MT_UNIT_LIGHT_TANK);
    CHECK(robot&&vehicle);Z_SetTeam(robot,0);Z_SetTeam(vehicle,0);
    ivec2_t rp=Z_BlastAnchor(robot),vp=ivec2_sub(Z_BlastAnchor(vehicle),(ivec2_t){8,8});
    ivec2_t rc={rp.x/Z_TILE,rp.y/Z_TILE},vc={vp.x/Z_TILE,vp.y/Z_TILE};
    CHECK(L_Contains(&level,rc.x,rc.y)&&L_Contains(&level,vc.x,vc.y));
    uint8_t *rt=&map->terrain[rc.y*Z_MAP_STRIDE+rc.x],*vt=&map->terrain[vc.y*Z_MAP_STRIDE+vc.x];
    *rt=0;Z_MobjTicker(robot);fixed_t base=robot->speed;
    CHECK(base==Z_LevelSpeed(4));
    *rt=7|0x80;Z_MobjTicker(robot);CHECK(robot->speed==Z_LevelSpeed(6));
    *rt=8;Z_MobjTicker(robot);CHECK(robot->speed==Z_LevelSpeed(2));
    *rt=9;Z_MobjTicker(robot);CHECK(robot->speed==Z_LevelSpeed(3));
    *rt=42;Z_MobjTicker(robot);CHECK(robot->speed==Z_LevelSpeed(1));
    *rt=31;Z_MobjTicker(robot);CHECK(robot->speed==Z_LevelSpeed(12));
    *rt=35;Z_MobjTicker(robot);CHECK(robot->speed==base); /* flag handler's speed term */
    *vt=0;Z_MobjTicker(vehicle);int original=vehicle->max_hp;fixed_t vehicle_base=vehicle->speed;
    *vt=7;Z_MobjTicker(vehicle);CHECK(vehicle->speed>vehicle_base);
    vehicle->hp=original/2-1;Z_BodyDamaged(vehicle,original);
    CHECK(vehicle->z.body_tracked);
    Z_MobjTicker(vehicle);CHECK(vehicle->speed==Z_LevelSpeed(vehicle->z.body_speed/10+2));
    *vt=42;Z_MobjTicker(vehicle);CHECK(vehicle->speed==Z_LevelSpeed(vehicle->z.body_speed/10-3));
    *vt=0;Z_MobjTicker(vehicle);CHECK(vehicle->speed==Z_LevelSpeed(vehicle->z.body_speed/10));
    vehicle->z.body_speed=19;*vt=42;Z_MobjTicker(vehicle);CHECK(vehicle->speed==Z_LevelSpeed(0));
    mobj_t *large=P_SpawnMobj((fixed3_t){0},MT_UNIT_FIRE_TRUCK);CHECK(large);Z_SetTeam(large,0);
    fixed2_t centre;
    CHECK(Z_UnitCentre(map,Z_UnitType(large),(ivec2_t){167,160},&centre));
    large->core.position=fixed3_from_fixed2(centre,0);
    ivec2_t engine_cell=fixed2_cell(centre);
    CHECK(engine_cell.x!=10||engine_cell.y!=10);
    map->terrain[10*Z_MAP_STRIDE+10]=7;
    map->terrain[engine_cell.y*Z_MAP_STRIDE+engine_cell.x]=42;
    Z_MobjTicker(large);CHECK(large->speed==Z_LevelSpeed(7)); /* native (167+8,160+8) */
    rts_game_model_destroy(model);
    puts("PASS: Z native terrain modifiers, clamping and damaged vehicle speed");
    return 0;
}
