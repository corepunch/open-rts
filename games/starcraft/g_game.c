#include "sc_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static mobjtype_t actors[SC_TYPES];
const char *const g_game_id="starcraft",*const g_game_name="StarCraft";
const char *const g_game_default_root="data/STARCRAFT";
const char *const g_game_default_map="catalog",*const g_game_default_sprite="sc-000";
const int g_cell_w=32,g_cell_h=32;
const uint16_t g_debug_enemy_type=1;
const gameinfo_t *gameinfo=&game_info;
const mobjtype_t *const actor_types=actors;
const int num_actor_types=SC_TYPES;
void G_InitGame(void) {
    sc_init_info();
    for(int i=0;i<SC_TYPES;i++) {
        const sc_unit_t *u=&sc_units[i];
        bool mobile=!(u->flags&1) && (u->orders==1||u->orders==2||u->orders==4||u->orders==5);
        actors[i]=(mobjtype_t){.id=i+1,.native_type_id=i,.name=u->name,.sprite_name=sc_names[i],
            .traits=MF_SELECTABLE|MF_RENDERABLE|(mobile?MF_MOBILE:0)|((u->flags&4)?MF_FLY:0),
            /* Catalog movement uses engine pacing; retail movement opcodes are not simulated. */
            .speed=mobile?3.0f:0,.max_hp=u->hp>0?u->hp:1,.sight={.day=u->sight,.night=u->sight},
            .footprint={(u->placement.w+31)/32,(u->placement.h+31)/32}};
    }
}
bool G_DoLoadLevel(const char *path,level_t *out) {
    if(strcmp(M_FileName(path),"catalog")) {
        fprintf(stderr,"starcraft: this basic implementation supports the catalog map; requested %s\n",path); return false;
    }
    memset(out,0,sizeof(*out));
    /* Authored catalog layout, not a retail mission. Every DAT slot gets a
     * distinct position, including heroes, subunits, unused slots and props. */
    out->width=128; out->height=128;
    size_t n=(size_t)out->width*out->height;
    out->tile_ids=malloc(n*sizeof(*out->tile_ids));
    out->blocked=calloc(n,1); out->cell_solid=calloc(n,1);
    if(!out->tile_ids||!out->blocked||!out->cell_solid) { P_FreeLevel(out); return false; }
    for(size_t i=0;i<n;i++) out->tile_ids[i]=(uint16_t)(32+(i%16));
    snprintf(out->map_path,sizeof(out->map_path),"%s",path);
    snprintf(out->tileset_name,sizeof(out->tileset_name),"badlands");
    out->sight.cells=malloc(n*sizeof(*out->sight.cells));
    if(!out->sight.cells) { P_FreeLevel(out); return false; }
    for(size_t i=0;i<n;i++) out->sight.cells[i]=SIGHT_EXPLORED;
    for(int i=0;i<8;i++) out->sight.allies[i]=SIGHT_EXPLORED;
    out->has_camera=true; out->camera=(fvec2_t){12,9};
    for(int i=0;i<8;i++) out->player_colors[i]=i;
    return true;
}
bool W_LoadAssets(const char *root,const level_t *map,const char *name,tileset_t *tiles,spritesheet_t *sprite) {
    (void)map; (void)name; memset(sprite,0,sizeof(*sprite));
    return sc_load_tiles(root,tiles);
}
int P_LoadThings(const char *path) {
    (void)path; int count=0;
    for(int i=0;i<SC_TYPES;i++) {
        /* The catalog is laid out in native top-left, Y-down pixel order. */
        ivec2_t pixel={192+(i%16)*240,192+(i/16)*240};
        fixed3_t pos={(fixed_t)(pixel.x*(FIXED_ONE/32)),(fixed_t)(pixel.y*(FIXED_ONE/32)),0};
        mobj_t *mo=P_SpawnMobj(pos,(uint16_t)(i+1)); if(!mo) break;
        mo->owner=consoleplayer; mo->team=consoleplayer; mo->allegiance=ALLEGIANCE_PLAYER;
        mo->core.angle=ANG270;
        if(!(mo->traits&MF_MOBILE)) {
            const mobjtype_t *a=&actors[i];
            ivec2_t cell={pixel.x/32-a->footprint.w/2,pixel.y/32-a->footprint.h/2};
            for(int y=0;y<a->footprint.h;y++) for(int x=0;x<a->footprint.w;x++)
                if(L_Contains(&level,cell.x+x,cell.y+y)) level.blocked[L_Index(&level,cell.x+x,cell.y+y)]=1;
        }
        ++count;
    }
    return count;
}
bool R_InitSprites(const char *root,const level_t *map,mobj_t *const *mobjs,int count,spritecache_t *cache) {
    (void)map;
    if(!sc_load_graphics(root,cache)) return false;
    for(int i=0;i<count;i++) P_SetMobjState(mobjs[i],mobjinfo[mobjs[i]->type_id].spawnstate);
    return true;
}
void G_MissionTicker(level_t *map,mobj_t *const *mobjs,int *count,hudtext_t *hud,float dt) {
    (void)map;(void)mobjs;(void)count;(void)hud;(void)dt;
}
bool G_UpdateProduction(level_t *map,mobj_t *const *units,int *count,float dt) {
    (void)map;(void)units;(void)count;(void)dt; return false;
}
irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0,0,app->win.w,app->win.h-128*app->win.h/480};
}
