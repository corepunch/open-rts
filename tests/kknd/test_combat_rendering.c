#include "game.h"
#include "info.h"
#include "rts_test.h"

#define CHECK(c) RTS_CHECK(c, "KKND combat rendering", #c)

/* Native point-0 coordinates in counterclockwise engine rotation order.
 * MOBD frame +24 -> {id,x<<8,y<<8,z}; documented in KKND_EXE_FINDINGS.md. */
static const struct {int type, native_frames; ivec2_t points[16];} weapons[] = {
    {MT_SURV_RIFLEMAN, 11, {{3,-21},{3,-21},{-7,-19},{-14,-12},{-14,-12},{-14,-12},{-11,-5},{-3,1},
                           {-3,1},{-3,1},{11,-5},{14,-12},{14,-12},{14,-12},{7,-19},{3,-21}}},
    {MT_SURV_DIRT_BIKE, 4, {{2,-10},{0,-10},{-3,-9},{-4,-8},{-6,-6},{-6,-3},{-6,-1},{-4,-1},
                          {-3,0},{4,-1},{6,-1},{6,-3},{6,-6},{4,-8},{3,-9},{0,-10}}},
    {MT_MUTE_DIRE_WOLF, 53, {{3,-14},{-1,-15},{-3,-15},{-7,-14},{-12,-10},{-12,-6},{-10,-3},{-8,-2},
                            {-4,-1},{8,-2},{10,-3},{12,-6},{12,-10},{7,-14},{3,-15},{1,-15}}},
    {MT_SURV_4X4_PICKUP, 4, {{0,3},{4,3},{6,2},{10,-1},{12,-5},{11,-9},{7,-12},{4,-13},
                           {0,-15},{-4,-13},{-7,-12},{-11,-9},{-12,-5},{-10,-1},{-6,2},{-4,3}}},
};
static const ivec2_t turret_points[16] = {
    {0,-14},{-2,-15},{-9,-14},{-12,-9},{-13,-5},{-13,-3},{-8,1},{-6,3},
    {0,5},{6,3},{8,1},{13,-3},{13,-5},{12,-9},{9,-14},{2,-15},
};

static int draw_at(SDL_Renderer *renderer, const spritesheet_t *sprite,
                   const spritelayer_t *part, ivec2_t origin) {
    const spritecell_t *cell = &sprite->cells[part->lump];
    ivec2_t anchor = cell->ground_point;
    bool flip = (part->flags & RTS_FRAME_FLIP_X) != 0;
    if (flip) anchor.x = cell->rect.w - anchor.x;
    ivec2_t corner = ivec2_sub(origin, anchor);
    irect_t dst = {corner.x, corner.y, cell->rect.w, cell->rect.h};
    CHECK(R_DrawSprite(renderer, sprite, part->lump, 0, NULL, &dst,
                      flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE,
                      (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND));
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,128,128,32,SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *catalog = SDL_CreateRGBSurfaceWithFormat(0,1024,1408,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface && catalog);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    SDL_FillRect(catalog, NULL, 0xff304030);
    app_t app = {.renderer=renderer, .win={128,128}, .cell={32,32}, .cam={-256,-240}};
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root="data/KKND"};
    CHECK(model && rts_game_model_load(model, &config));
    spritecache_t *cache = calloc(1,sizeof(*cache));
    CHECK(cache);
    mobjlist_t objects = P_ListMobjs();
    CHECK(R_InitSprites(renderer,config.data_root,&level,objects.items,objects.count,cache));
    P_FreeMobjList(&objects);
    P_FreeThinkers();
    free(level.sight.cells);
    level.sight.cells = NULL;
    const spritesheet_t *extras = R_StateSprite(cache,gameinfo,SPR_EXTRAS,NULL);
    CHECK(extras && extras->spritedef.numframes == 212);
    uint32_t expected[128*128], actual[128*128];
    for (size_t type = 0; type < sizeof(weapons)/sizeof(*weapons); ++type) {
        mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){10,10},0),weapons[type].type);
        CHECK(unit);
        CHECK(P_VisibleToPlayer(unit));
        const mobjinfo_t *info = &mobjinfo[unit->type_id];
        const spritesheet_t *sprite = R_StateSprite(cache,gameinfo,unit->core.sprite_id,NULL);
        CHECK(sprite && sprite->spritedef.numframes == weapons[type].native_frames + 2);
        for (int rotation=0; rotation<16; ++rotation) {
            unit->core.angle = direction_to_angle(rotation,16,ANG90,false);
            CHECK(P_SetMobjState(unit,info->missilestate));
            for (int phase=0; phase<2; ++phase) {
                CHECK(unit->core.frame == weapons[type].native_frames + phase && unit->core.tics == 2);
                const spritelayer_t *body = sprite->spritedef.spriteframes[info->muzzle.body].directions[rotation].layers;
                const spritelayer_t *flash = extras->spritedef.spriteframes[info->muzzle.frame+phase].directions[rotation].layers;
                const spritelayer_t *composed = sprite->spritedef.spriteframes[unit->core.frame].directions[rotation].layers;
                int parts = info->muzzle.turret ? 3 : 2;
                CHECK(!strcmp(composed[parts-1].sprite_name,"22") && !composed[parts].sprite_name[0]);
                SDL_SetRenderDrawColor(renderer,48,64,48,255);
                SDL_RenderClear(renderer);
                RTS_RUN(draw_at(renderer,sprite,body,(ivec2_t){64,80}));
                ivec2_t muzzle = ivec2_add((ivec2_t){64,80},weapons[type].points[rotation]);
                if (info->muzzle.turret) {
                    const spritesheet_t *turret = R_StateSprite(cache,gameinfo,info->muzzle.turret,NULL);
                    CHECK(turret && !strcmp(body[1].sprite_name,"47"));
                    RTS_RUN(draw_at(renderer,turret,body+1,muzzle));
                    muzzle = ivec2_add(muzzle,turret_points[rotation]);
                }
                RTS_RUN(draw_at(renderer,extras,flash,muzzle));
                CHECK(SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,expected,128*4)==0);
                SDL_RenderClear(renderer);
                R_DrawThings(&app,&unit,1,NULL,cache,gameinfo,0);
                CHECK(SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,actual,128*4)==0);
                if (memcmp(expected,actual,sizeof(actual))!=0) {
                    fprintf(stderr,"render mismatch type=%d rotation=%d phase=%d body_lump=%d flash_lump=%d offset=(%d,%d)\n",
                            unit->type_id,rotation,phase,body->lump,flash->lump,
                            composed[parts-1].offset.x,composed[parts-1].offset.y);
                    CHECK(false);
                }
                int visible=0;
                for (size_t pixel=0; pixel<128*128; ++pixel) visible += actual[pixel]!=0xff304030;
                CHECK(visible>0);
                if (!(rotation%2)) {
                    SDL_Rect dest = {(rotation/2)*128,((int)type*2+phase)*128,128,128};
                    CHECK(SDL_BlitSurface(surface,NULL,catalog,&dest)==0);
                }
                CHECK(P_TickMobjState(unit));
                CHECK(P_TickMobjState(unit));
            }
        }
        P_RemoveMobj(unit);
    }
    const int deaths[] = {MT_SURV_RIFLEMAN,MT_MUTE_BERSERKER,MT_MUTE_DIRE_WOLF};
    for (int i=0; i<3; ++i) {
        mobj_t *unit=P_SpawnMobj(fixed3_from_fvec2((fvec2_t){10,10},0),deaths[i]);
        CHECK(unit);
        P_DamageMobj(unit,NULL,unit->hp);
        for (int frame=0; !unit->remove; ++frame) {
            if (!(frame%2)) {
                SDL_RenderClear(renderer);
                R_DrawThings(&app,&unit,1,NULL,cache,gameinfo,0);
                SDL_Rect dest={(frame/2)*128,(8+i)*128,128,128};
                CHECK(SDL_BlitSurface(surface,NULL,catalog,&dest)==0);
            }
            int tics=unit->core.tics;
            for (int t=0;t<tics;++t) P_MobjThinker(unit);
        }
    }
    const char *path=getenv("OPEN_RTS_TEST_COMBAT_BMP");
    if (path) CHECK(SDL_SaveBMP(catalog,path)==0);
    R_FreeSpriteCache(cache);
    free(cache);
    rts_game_model_destroy(model);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_FreeSurface(catalog);
    SDL_Quit();
    puts("PASS: both muzzle phases in all 16 facings match native attachment-point rendering");
    return 0;
}
