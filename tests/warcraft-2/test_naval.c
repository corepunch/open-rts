#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II navy", #c)

static mobj_t *spawn(int type, fvec2_t at) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = 0;
    unit->allegiance = ALLEGIANCE_PLAYER;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

static void fixture(void) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = 32;
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.cell_terrain = calloc(1024, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 5;
    level.speeds->terrain[1][0] = 100;
    level.speeds->terrain[2][1] = 100;
    level.speeds->terrain[4][1] = level.speeds->terrain[4][3] = 100;
    for (int y = 0; y < 32; ++y) for (int x = 10; x < 32; ++x) {
        level.cell_terrain[L_Index(&level, x, y)] = x == 10 ? 3 : 1;
        level.blocked[L_Index(&level, x, y)] = 1;
    }
    for (int r = 0; r < 3; ++r) level.player_resources[0][r] = 10000;
}

static mobj_t *find_type(int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && !unit->remove && unit->hp > 0 && unit->type_id == type)
            return unit;
    }
    return NULL;
}

static int launch(int side) {
    fixture();
    spawn(MT_FARM + side, (fvec2_t){3, 3});
    mobj_t *yard = spawn(MT_HUMAN_SHIPYARD + side, (fvec2_t){11.5f, 5.5f});
    w2_mark_footprint(10, 4, (isize2_t){3, 3});
    const StaticProductDefinition *product = G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, MT_HUMAN_OIL_TANKER + side);
    CHECK(product && G_PlayerBuildProduct(yard, product));
    G_ProductionTicker(20);
    mobj_t *tanker = find_type(MT_HUMAN_OIL_TANKER + side);
    CHECK(tanker);
    fvec2_t at = fixed3_xy_to_fvec2(tanker->core.position);
    CHECK(P_CheckPosition(&level, tanker, at.x, at.y));
    CHECK(level.cell_terrain[L_Index(&level, (int)at.x, (int)at.y)] == 1);
    CHECK(!P_CheckPosition(&level, tanker, 8.5f, 8.5f));
    CHECK(!P_CheckPosition(&level, tanker, 10.5f, 8.5f));
    CHECK(P_MoveUnitTo(&level, tanker, (fvec2_t){18.5f, 9.5f}));
    for (int i = 0; i < 1000 && P_HasMoveOrder(tanker); ++i) P_Ticker();
    CHECK(fvec2_near(fixed3_xy_to_fvec2(tanker->core.position), (fvec2_t){18.5f, 9.5f}, 0.01f));
    mobj_t *patch = spawn(MT_OIL_PATCH, (fvec2_t){22.5f, 9.5f});
    patch->owner = patch->team = 15; patch->allegiance = ALLEGIANCE_NEUTRAL;
    level.resource_vents = calloc(1, sizeof(*level.resource_vents));
    level.resource_vent_count = 1;
    resourcevent_t *vent = level.resource_vents;
    *vent = (resourcevent_t){.cell = {21, 8}, .footprint = {3, 3}, .attachment = {22.5f, 9.5f},
        .amount = 250, .resource_type = 2, .source_id = patch->id, .active = true};
    w2_mark_footprint(21, 8, vent->footprint);
    CHECK(!W2_HarvestOrder(tanker, vent->attachment));
    CHECK(W2_ConstructOrder(tanker, MT_HUMAN_OIL_PLATFORM + side, vent->cell));
    for (int i = 0; i < 3000 && !tanker->w2.site; ++i) P_Ticker();
    mobj_t *platform = P_MobjById(vent->source_id);
    CHECK(platform && W2_UnderConstruction(platform));
    CHECK(states[platform->core.state_id].sprite == W2_NAVAL_SITE_SPRITE + 2 + side);
    CHECK(!P_VentOpenTo(&level, vent, tanker));
    for (int i = 0; i < 2000 && W2_UnderConstruction(platform); ++i) P_Ticker();
    CHECK(!W2_UnderConstruction(platform) && tanker->harvest.phase != HARVEST_PHASE_NONE);
    level.player_resources[0][2] = 0;
    bool pumping = false, loaded = false;
    for (int i = 0; i < 5000 && !level.player_resources[0][2]; ++i) {
        P_Ticker();
        pumping |= platform->core.frame == 2;
        loaded |= states[tanker->core.state_id].sprite == W2_TANK_FULL_SPRITE + side;
    }
    CHECK(pumping && loaded);
    CHECK(level.player_resources[0][2] == 100 && vent->amount == 150);
    mobj_t *refinery = spawn(MT_HUMAN_REFINERY + side, (fvec2_t){11.5f, 17.5f});
    w2_mark_footprint(10, 16, (isize2_t){3, 3});
    refinery->w2.build_left_tics = 1;
    CHECK(W2_ResourceIncome(0, 2) == 100);
    refinery->w2.build_left_tics = 0;
    CHECK(W2_ResourceIncome(0, 2) == 125);
    for (int i = 0; i < 8000 && vent->active; ++i) P_Ticker();
    CHECK(!vent->active && vent->amount == 0 && !P_MobjById(vent->source_id));
    for (int i = 0; i < 4000 && tanker->harvest.cargo; ++i) P_Ticker();
    CHECK(level.player_resources[0][2] == 287 && !tanker->harvest.cargo);
    CHECK(P_CheckPosition(&level, tanker, 22.5f, 9.5f));
    return 0;
}

static int shore(void) {
    fixture();
    mobj_t *worker = spawn(MT_PEASANT, (fvec2_t){8.5f, 5.5f});
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){7, 4}, worker));
    CHECK(W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){10, 4}, worker));
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){11, 4}, worker));
    CHECK(!W2_CanPlace(MT_FARM, (ivec2_t){10, 4}, worker));
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){10, -1}, worker));
    mobj_t *patch = spawn(MT_OIL_PATCH, (fvec2_t){16.5f, 5.5f});
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){10, 4}, worker));
    patch->core.position = fixed3_from_fvec2((fvec2_t){17.5f, 5.5f}, 0);
    CHECK(W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){10, 4}, worker));
    P_RemoveMobj(patch);
    CHECK(W2_ConstructOrder(worker, MT_HUMAN_SHIPYARD, (ivec2_t){10, 4}));
    for (int i = 0; i < 3000; ++i) P_Ticker();
    mobj_t *yard = find_type(MT_HUMAN_SHIPYARD);
    CHECK(yard && !W2_UnderConstruction(yard));
    mobj_t *transport = spawn(MT_HUMAN_TRANSPORT, (fvec2_t){13.5f, 12.5f});
    CHECK(P_CheckPosition(&level, transport, 10.5f, 12.5f));
    CHECK(!P_CheckPosition(&level, transport, 9.5f, 12.5f));
    CHECK(P_MoveUnitTo(&level, transport, (fvec2_t){10.5f, 12.5f}));
    for (int i = 0; i < 1000 && P_HasMoveOrder(transport); ++i) P_Ticker();
    mobj_t *passenger = spawn(MT_FOOTMAN, (fvec2_t){9.5f, 12.5f});
    CHECK(W2_BoardOrder(passenger, transport));
    P_Ticker(); CHECK(passenger->w2.boarded);
    CHECK(W2_UnloadOrder(transport, fixed3_xy_to_fvec2(transport->core.position)));
    for (int i = 0; i < 10; ++i) P_Ticker();
    CHECK(!passenger->w2.boarded);
    fvec2_t at = fixed3_xy_to_fvec2(passenger->core.position);
    CHECK(P_CheckPosition(&level, passenger, at.x, at.y));
    return 0;
}

static int fleet(int side) {
    const int types[] = {MT_HUMAN_TRANSPORT, MT_HUMAN_DESTROYER, MT_BATTLESHIP, MT_GNOMISH_SUBMARINE};
    for (int i = 0; i < 4; ++i) {
        fixture();
        spawn(MT_FARM + side, (fvec2_t){3, 3});
        spawn(MT_HUMAN_FOUNDRY + side, (fvec2_t){5, 3});
        spawn(MT_INVENTOR + side, (fvec2_t){5, 6});
        mobj_t *yard = spawn(MT_HUMAN_SHIPYARD + side, (fvec2_t){11.5f, 5.5f});
        w2_mark_footprint(10, 4, (isize2_t){3, 3});
        int type = types[i] + side;
        const StaticProductDefinition *product = G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, type);
        int oil = level.player_resources[0][2];
        CHECK(product && G_PlayerBuildProduct(yard, product));
        CHECK(level.player_resources[0][2] == oil - mobjinfo[type].w2.costs.resources[2]);
        G_ProductionTicker(30);
        mobj_t *ship = find_type(type);
        CHECK(ship);
        fvec2_t at = fixed3_xy_to_fvec2(ship->core.position);
        CHECK(P_CheckPosition(&level, ship, at.x, at.y));
        if (i > 0) {
            ship->core.position = fixed3_from_fvec2((fvec2_t){18.5f, 12.5f}, 0);
            mobj_t *target = spawn(MT_ORC_OIL_TANKER - side, (fvec2_t){21.5f, 12.5f});
            target->owner = target->team = 1; target->allegiance = ALLEGIANCE_ENEMY;
            target->max_hp = target->hp = 10000;
            ship->attack.target = target;
            CHECK(P_SetMobjState(ship, mobjinfo[type].missilestate));
            if (i < 3) CHECK(ship->core.frame == 0);
            for (int t = 0; t < 80; ++t) {
                P_Ticker();
                if (i < 3) CHECK(ship->core.frame == 0);
            }
            CHECK(target->hp < 10000);
        }
        uint32_t id = ship->id;
        P_DamageMobj(ship, NULL, ship->hp);
        if (i < 3) {
            CHECK(ship->core.frame == 1 && states[ship->core.state_id].group == W2_GROUP_DEATH);
            for (int t = 0; t < 50; ++t) P_Ticker();
            CHECK(ship->core.frame == 2);
        }
        for (int t = 0; t < 60; ++t) P_Ticker();
        CHECK(!P_MobjById(id));
    }
    return 0;
}

/* Native contact sheet: each era/faction row is empty tanker, loaded tanker,
 * the two well construction stages, idle platform, pumping platform. */
static int native_art(const char *path) {
    w2_archive_t arc;
    CHECK(w2_archive_open(&arc, "data/WAR2/DATA/MAINDAT.WAR"));
    SDL_Surface *image = path ? SDL_CreateRGBSurfaceWithFormat(0, 672, 896, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (path) { CHECK(image); SDL_FillRect(image, NULL, 0xff304858); }
    for (int era = 0; era < 4; ++era) {
        spritecache_t cache = {0};
        w2_blob_t blob = {0};
        uint32_t palette[256];
        CHECK(w2_archive_extract(&arc, w2_era_palette(era), &blob) && w2_decode_palette(&blob, palette));
        w2_blob_free(&blob);
        CHECK(w2_load_shared_sprites(&arc, palette, era, &cache));
        for (int side = 0; side < 2; ++side) {
            spritesheet_t empty = {0}, platform = {0};
            CHECK(w2_archive_extract(&arc, 59 + side, &blob) && w2_decode_grp(&blob, palette, &empty, true, NULL));
            w2_blob_free(&blob);
            int entry = w2_grp_entry(&mobjinfo[MT_HUMAN_OIL_PLATFORM + side], era, arc.count);
            CHECK(w2_archive_extract(&arc, entry, &blob) && w2_decode_grp(&blob, palette, &platform, false, NULL));
            w2_blob_free(&blob);
            const cachedsprite_t *full = R_CacheFind(&cache, sprnames[W2_TANK_FULL_SPRITE + side]);
            const cachedsprite_t *site = R_CacheFind(&cache, sprnames[W2_NAVAL_SITE_SPRITE + 2 + side]);
            CHECK(full && site && full->sprite.spritedef.numframes == 3 && site->sprite.numlumps >= 2 && platform.numlumps == 3);
            CHECK(full->sprite.spritedef.spriteframes[0].rotations == 8);
            for (int building = 0; building < 8; ++building) {
                const cachedsprite_t *art = R_CacheFind(&cache, sprnames[W2_NAVAL_SITE_SPRITE + building]);
                CHECK(art && art->sprite.numlumps >= 2);
            }
            if (image) {
                const spritesheet_t *sheets[] = {&empty, &full->sprite, &site->sprite, &site->sprite, &platform, &platform};
                const int cells[] = {2, 2, 0, 1, 0, 2};
                for (int col = 0; col < 6; ++col) {
                    const spritesheet_t *sheet = sheets[col];
                    const spritecell_t *cell = &sheet->cells[cells[col]];
                    CHECK(cell->rect.w <= 112 && cell->rect.h <= 112);
                    for (int y = 0; y < cell->rect.h; ++y) for (int x = 0; x < cell->rect.w; ++x) {
                        uint8_t ink = sheet->lumps[cells[col]].indices[y * cell->rect.w + x];
                        if (!ink) continue;
                        int dx = col * 112 + (112 - cell->rect.w) / 2 + x;
                        int dy = (era * 2 + side) * 112 + (112 - cell->rect.h) / 2 + y;
                        ((uint32_t *)((uint8_t *)image->pixels + dy * image->pitch))[dx] = sheet->palette[ink];
                    }
                }
            }
            R_FreeSprite(&empty); R_FreeSprite(&platform);
        }
        R_FreeSpriteCache(&cache);
    }
    if (image) {
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
        CHECK(rgb && SDL_SaveBMP(rgb, path) == 0);
        SDL_FreeSurface(rgb); SDL_FreeSurface(image);
    }
    w2_archive_close(&arc);
    return 0;
}

int main(int argc, char **argv) {
    RTS_RUN(launch(0)); RTS_RUN(launch(1));
    RTS_RUN(shore());
    RTS_RUN(fleet(0)); RTS_RUN(fleet(1));
    RTS_RUN(native_art(argc > 1 ? argv[1] : NULL));
    P_FreeLevel(&level);
    puts("PASS: both navies launch, move, build platforms, gather oil, refine and exhaust reserves; shoreline placement");
    return 0;
}
