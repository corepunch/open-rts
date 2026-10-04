#include "t_local.h"
#include "w2_local.h"

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
    RTS_CHECK(consoleplayer == 1, "alamo", "human slot");
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
    bool saw_grunt = false, saw_hall = false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        RTS_CHECK(unit->type_id != 95 && unit->type_id != 96, "alamo", "no start marker");
        RTS_CHECK(unit->type_id != 104 && unit->type_id != 105, "alamo", "no wall unit");
        if (unit->owner == 1) {
            RTS_CHECK(unit->allegiance == ALLEGIANCE_PLAYER, "alamo", "human allegiance");
            if (unit->type_id == 75) {
                saw_hall = true;
                RTS_CHECK((unit->traits & MF_SELECTABLE) != 0, "alamo", "hall selectable");
            }
        }
        if (strcmp(unit->core.sprite_name, "grunt") == 0) {
            saw_grunt = true;
            RTS_CHECK(unit->team == unit->owner, "alamo", "grunt team");
        }
    }
    RTS_CHECK(saw_grunt, "alamo", "grunt");
    RTS_CHECK(saw_hall, "alamo", "town hall");

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

static int sheet_wh(const spritesheet_t *sheet, int frame, int w, int h) {
    return sheet && sheet->cells && frame >= 0 && frame < sheet->numlumps &&
           sheet->cells[frame].rect.w == w && sheet->cells[frame].rect.h == h;
}

static int test_ui_art(void) {
    w2_menu_art_t menu = { 0 };
    w2_hud_art_t hud = { 0 };
    RTS_CHECK(w2_load_menu_art("data/WAR2", &menu), "ui", "menu art");
    RTS_CHECK(sheet_wh(&menu.title, 0, 640, 480), "ui", "title");
    RTS_CHECK(sheet_wh(&menu.panel[0], 0, 256, 288) &&
              sheet_wh(&menu.panel[1], 0, 256, 288), "ui", "panels");
    RTS_CHECK(menu.widgets[0].numlumps >= 18 && menu.widgets[1].numlumps >= 18 &&
              sheet_wh(&menu.widgets[0], 10, 106, 28) &&
              sheet_wh(&menu.widgets[0], 16, 224, 28) &&
              sheet_wh(&menu.widgets[1], 11, 106, 28) &&
              sheet_wh(&menu.widgets[1], 17, 224, 28), "ui", "widgets");
    RTS_CHECK(menu.font.glyph_index[(unsigned)'M'] >= 0 &&
              menu.font.glyph_width[(unsigned)'M'] == 10 &&
              menu.font.glyph_width[(unsigned)' '] > 0, "ui", "font");
    int frame = menu.font.glyph_index[(unsigned)'M'];
    const uint8_t *ink_px = menu.font.sprite.lumps[frame].indices;
    int ink = 0;
    int pixels = menu.font.sprite.cells[frame].rect.w * menu.font.sprite.cells[frame].rect.h;
    for (int i = 0; i < pixels; ++i) if (ink_px[i]) ink++;
    RTS_CHECK(ink > 20, "ui", "font ink");

    RTS_CHECK(w2_load_hud_art("data/WAR2", 0, false, &hud), "ui", "hud art");
    RTS_CHECK(sheet_wh(&hud.menu_button, 0, 176, 24), "ui", "menu button");
    RTS_CHECK(sheet_wh(&hud.minimap, 0, 176, 136), "ui", "minimap");
    RTS_CHECK(sheet_wh(&hud.info, 0, 176, 176), "ui", "info");
    RTS_CHECK(sheet_wh(&hud.buttons, 0, 176, 144), "ui", "command panel");
    RTS_CHECK(sheet_wh(&hud.resource, 0, 448, 16) &&
              sheet_wh(&hud.status, 0, 448, 16), "ui", "bars");
    RTS_CHECK(sheet_wh(&hud.filler, 0, 16, 480), "ui", "filler");
    RTS_CHECK(hud.icons.numlumps == 186, "ui", "icons");
    RTS_CHECK(hud.font.glyph_index[(unsigned)'0'] >= 0, "ui", "hud font");
    w2_free_hud_art(&hud);
    RTS_CHECK(w2_load_hud_art("data/WAR2", 0, true, &hud), "ui", "orc hud");
    RTS_CHECK(sheet_wh(&hud.menu_button, 0, 176, 24) && hud.icons.numlumps == 186,
              "ui", "orc chrome");

    w2_free_menu_art(&menu);
    w2_free_hud_art(&hud);
    return 0;
}

int main(void) {
    RTS_RUN(test_alamo());
    RTS_RUN(test_channel());
    RTS_RUN(test_ui_art());
    printf("warcraft2 pud tests passed\n");
    return 0;
}
