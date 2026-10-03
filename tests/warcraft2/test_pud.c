#include "t_local.h"

#include <stdio.h>
#include <string.h>

static int count_terrain(uint8_t kind) {
    int count = 0;
    int cells = level.width * level.height;
    for (int i = 0; i < cells; ++i)
        if (level.cell_terrain && level.cell_terrain[i] == kind) count++;
    return count;
}

static int count_units(void) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) count++;
    return count;
}

static const uint8_t *tile_image(const tileset_t *tileset, int value) {
    if (!tileset->tile_lookup || value < 0 || value >= tileset->tile_lookup_count) return NULL;
    int tile = tileset->tile_lookup[value];
    if (tile < 0 || tile >= tileset->count || !tileset->indices) return NULL;
    return tileset->indices + (size_t)tile * (size_t)tileset->tile_w * (size_t)tileset->tile_h;
}

static int test_alamo(void) {
    G_InitGame();
    P_InitThinkers();
    RTS_CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level), "alamo", "load");
    RTS_CHECK(level.width == 96 && level.height == 96, "alamo", "size");
    RTS_CHECK(strcmp(level.tileset_name, "forest") == 0, "alamo", "era");
    int land = count_terrain(0);
    int water = count_terrain(1);
    int forest = count_terrain(2);
    fprintf(stderr, "alamo land=%d water=%d forest=%d\n", land, water, forest);
    RTS_CHECK(water == 0 && land > 6000 && forest > 2500, "alamo", "terrain");
    RTS_CHECK(level.player_resources[1][0] > 0, "alamo", "human gold");

    tileset_t tileset = { 0 };
    spritesheet_t footman = { 0 };
    RTS_CHECK(W_LoadAssets("data/WAR2", &level, "footman", &tileset, &footman), "alamo", "assets");
    RTS_CHECK(tileset.count > 16, "alamo", "megatiles");
    RTS_CHECK(footman.numlumps == 60, "alamo", "footman lumps");
    RTS_CHECK(footman.spritedef.numframes >= 5 &&
              footman.spritedef.spriteframes[0].rotations == 8, "alamo", "footman facings");

    const uint8_t *grass = NULL, *trees = NULL;
    int cells = level.width * level.height;
    for (int i = 0; i < cells && (!grass || !trees); ++i) {
        uint16_t value = level.tile_ids[i];
        if (!grass && value >= 0x50 && value <= 0x5f) grass = tile_image(&tileset, value);
        if (!trees && value >= 0x70 && value <= 0x7f) trees = tile_image(&tileset, value);
    }
    RTS_CHECK(grass && trees && memcmp(grass, trees, 32u * 32u) != 0, "alamo", "tile art");

    int spawned = P_LoadThings(NULL);
    fprintf(stderr, "alamo spawned=%d thinkers=%d\n", spawned, count_units());
    RTS_CHECK(spawned == 64 && count_units() == 64, "alamo", "spawn count");
    bool saw_grunt = false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        RTS_CHECK(unit->type_id != 95 && unit->type_id != 96, "alamo", "no start marker");
        RTS_CHECK(unit->type_id != 104 && unit->type_id != 105, "alamo", "no wall unit");
        if (strcmp(unit->core.sprite_name, "grunt") == 0) {
            saw_grunt = true;
            RTS_CHECK(unit->team == unit->owner, "alamo", "grunt team");
        }
    }
    RTS_CHECK(saw_grunt, "alamo", "grunt");

    mobjlist_t list = P_ListMobjs();
    spritecache_t cache = { 0 };
    bool sprites_ok = R_InitSprites("data/WAR2", &level, list.items, list.count, &cache);
    fprintf(stderr, "alamo sprites ok=%d cache=%d\n", sprites_ok, cache.count);
    cachedsprite_t *grunt = R_CacheFind(&cache, "grunt");
    RTS_CHECK(grunt && grunt->sprite.spritedef.numframes >= 5 &&
              grunt->sprite.spritedef.spriteframes[0].rotations == 8, "alamo", "grunt frames");
    const spritepalettemap_t *blue = NULL;
    for (int i = 0; i < grunt->sprite.palette_map_count; ++i)
        if (grunt->sprite.palette_maps[i].id == 1) blue = &grunt->sprite.palette_maps[i];
    RTS_CHECK(blue != NULL, "alamo", "team map");
    RTS_CHECK(blue->indices[208] == 212 && blue->indices[209] == 213 &&
              blue->indices[210] == 214 && blue->indices[211] == 215,
              "alamo", "blue remap");

    P_FreeMobjList(&list);
    R_FreeSpriteCache(&cache);
    R_FreeSprite(&footman);
    R_FreeTileset(&tileset);
    P_FreeLevel(&level);
    return 0;
}

static int test_channel(void) {
    P_InitThinkers();
    RTS_CHECK(G_DoLoadLevel("data/WAR2/CHANNEL.PUD", &level), "channel", "load");
    int land = count_terrain(0);
    int water = count_terrain(1);
    fprintf(stderr, "channel land=%d water=%d era=%s\n", land, water, level.tileset_name);
    RTS_CHECK(water > 3000 && land > 0, "channel", "water");
    RTS_CHECK(strcmp(level.tileset_name, "wasteland") == 0, "channel", "era");
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    RTS_RUN(test_alamo());
    RTS_RUN(test_channel());
    printf("warcraft2 pud tests passed\n");
    return 0;
}
