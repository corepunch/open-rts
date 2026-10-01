#include "engine.h"
#include "info.h"
#include "t_local.h"

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

enum { VIEW = 128 };
static const uint32_t CLEAR = 0xff304030u;

static int draw_at(const spritesheet_t *sprite, const spritelayer_t *part, ivec2_t origin) {
    const spritecell_t *cell = &sprite->cells[part->lump];
    ivec2_t anchor = cell->ground_point;
    bool flip = (part->flags & RTS_FRAME_FLIP_X) != 0;
    if (flip) anchor.x = cell->rect.w - anchor.x;
    ivec2_t corner = ivec2_sub(origin, anchor);
    irect_t dst = {corner.x, corner.y, cell->rect.w, cell->rect.h};
    CHECK(R_DrawSprite(sprite, part->lump, 0, NULL, &dst, flip ? V_FLIP_X : 0, 16));
    return 0;
}

/* Copy one read-back view into the optional contact sheet. */
static void catalog_view(SDL_Surface *catalog, const uint32_t *view, int column, int row) {
    for (int y = 0; y < VIEW; ++y)
        memcpy((uint8_t *)catalog->pixels + (size_t)(row * VIEW + y) * (size_t)catalog->pitch +
               (size_t)column * VIEW * sizeof(uint32_t), view + y * VIEW, VIEW * sizeof(uint32_t));
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *catalog = SDL_CreateRGBSurfaceWithFormat(0,1024,1408,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(catalog);
    V_AllocScreen(VIEW, VIEW);
    CHECK(screens[0].pixels);
    SDL_FillRect(catalog, NULL, CLEAR);
    app_t app = {.win={VIEW,VIEW}, .cell={32,32}, .cam={-256,-240}};
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root="data/KKND"};
    CHECK(model && rts_game_model_load(model, &config));
    spritecache_t *cache = calloc(1,sizeof(*cache));
    CHECK(cache);
    mobjlist_t objects = P_ListMobjs();
    CHECK(R_InitSprites(config.data_root,&level,objects.items,objects.count,cache));
    P_FreeMobjList(&objects);
    P_FreeThinkers();
    free(level.sight.cells);
    level.sight.cells = NULL;
    const spritesheet_t *extras = R_StateSprite(cache,gameinfo,SPR_EXTRAS,NULL);
    CHECK(extras && extras->spritedef.numframes == 212);
    I_SetPalette(extras->source_palette);
    static uint32_t expected[VIEW*VIEW], actual[VIEW*VIEW];
    /* The clear colour reads back as its nearest screen-palette entry. */
    uint32_t clear = vpalette[V_NearestIndex(CLEAR)] | 0xff000000u;
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
                V_BeginFrame(CLEAR);
                RTS_RUN(draw_at(sprite,body,(ivec2_t){64,80}));
                ivec2_t muzzle = ivec2_add((ivec2_t){64,80},weapons[type].points[rotation]);
                if (info->muzzle.turret) {
                    const spritesheet_t *turret = R_StateSprite(cache,gameinfo,info->muzzle.turret,NULL);
                    CHECK(turret && !strcmp(body[1].sprite_name,"47"));
                    RTS_RUN(draw_at(turret,body+1,muzzle));
                    muzzle = ivec2_add(muzzle,turret_points[rotation]);
                }
                RTS_RUN(draw_at(extras,flash,muzzle));
                V_ReadPixels(expected,VIEW*4);
                V_BeginFrame(CLEAR);
                R_DrawThings(&app,&unit,1,NULL,cache,gameinfo,0);
                V_ReadPixels(actual,VIEW*4);
                if (memcmp(expected,actual,sizeof(actual))!=0) {
                    fprintf(stderr,"render mismatch type=%d rotation=%d phase=%d body_lump=%d flash_lump=%d offset=(%d,%d)\n",
                            unit->type_id,rotation,phase,body->lump,flash->lump,
                            composed[parts-1].offset.x,composed[parts-1].offset.y);
                    CHECK(false);
                }
                int visible=0;
                for (size_t pixel=0; pixel<VIEW*VIEW; ++pixel) visible += actual[pixel]!=clear;
                CHECK(visible>0);
                if (!(rotation%2)) catalog_view(catalog,actual,rotation/2,(int)type*2+phase);
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
            if (!(frame%2) && frame/2 < catalog->w/VIEW) {
                V_BeginFrame(CLEAR);
                R_DrawThings(&app,&unit,1,NULL,cache,gameinfo,0);
                V_ReadPixels(actual,VIEW*4);
                catalog_view(catalog,actual,frame/2,8+i);
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
    V_FreeScreen();
    SDL_FreeSurface(catalog);
    SDL_Quit();
    puts("PASS: both muzzle phases in all 16 facings match native attachment-point rendering");
    return 0;
}
