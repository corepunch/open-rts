#include "t_local.h"
#include "w2_local.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

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
    RTS_CHECK(level.player_resources[1][0] == 3000 &&
              level.player_resources[1][1] == 1400 &&
              level.player_resources[1][2] == 2000, "alamo", "human stock");
    RTS_CHECK(level.player_resources[0][1] == 1000 &&
              level.player_resources[0][2] == 1000, "alamo", "empty slot stock");

    tileset_t tileset = { 0 };
    spritesheet_t footman = { 0 };
    RTS_CHECK(W_LoadAssets("data/WAR2", &level, "footman", &tileset, &footman), "alamo", "assets");
    RTS_CHECK(tileset.count > 16, "alamo", "megatiles");
    RTS_CHECK(footman.numlumps == 60, "alamo", "footman lumps");
    RTS_CHECK(ivec2_equal(footman.cells[0].ground_point, (ivec2_t){36, 36}),
              "alamo", "center the native 72x72 canvas on the footprint");
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
    RTS_CHECK(level.resource_vent_count == 2798, "alamo", "2787 trees and 11 gold mines");
    const w2_pud_t *pud = level.native_data;
    for (int i = 0; i < pud->unit_count; ++i) {
        const w2_pud_unit_t *rec = &pud->units[i];
        if (rec->type != 92) continue;
        bool found = false;
        for (int v = 0; v < level.resource_vent_count; ++v) {
            const resourcevent_t *vent = &level.resource_vents[v];
            if (!vent->source_id || !ivec2_equal(vent->cell, (ivec2_t){rec->x, rec->y})) continue;
            RTS_CHECK(vent->amount == rec->data * 2500, "alamo", "PUD mine reserves");
            found = true;
        }
        RTS_CHECK(found, "alamo", "mine source");
    }
    /* Match the interactive startup: sight is published, then single-player
     * net setup runs, then the ticker publishes sight again. */
    RTS_CHECK(P_InitSight(), "alamo", "sight");
    P_UpdateSight();
    int net_argc = 1;
    char *net_argv[] = { "test", NULL };
    RTS_CHECK(I_InitNetwork(&net_argc, net_argv), "alamo", "net init");
    D_CheckNetGame(1);
    RTS_CHECK(consoleplayer == 1, "alamo", "human slot after net setup");
    P_UpdateSight();
    bool saw_grunt = false, saw_hall = false;
    int visible_own = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        RTS_CHECK(unit->type_id != 95 && unit->type_id != 96, "alamo", "no start marker");
        RTS_CHECK(unit->type_id != 104 && unit->type_id != 105, "alamo", "no wall unit");
        if (unit->owner == 1) {
            RTS_CHECK(unit->allegiance == ALLEGIANCE_PLAYER, "alamo", "human allegiance");
            if (P_VisibleToPlayer(unit)) visible_own++;
            if (unit->type_id == 75) {
                saw_hall = true;
                RTS_CHECK((unit->traits & MF_SELECTABLE) != 0, "alamo", "hall selectable");
                RTS_CHECK(P_VisibleToPlayer(unit), "alamo", "town hall visible");
            }
        }
        if (strcmp(unit->core.sprite_name, "grunt") == 0) {
            saw_grunt = true;
            RTS_CHECK(unit->team == unit->owner, "alamo", "grunt team");
        }
    }
    RTS_CHECK(saw_grunt, "alamo", "grunt");
    RTS_CHECK(saw_hall, "alamo", "town hall");
    fprintf(stderr, "alamo visible human units=%d\n", visible_own);
    RTS_CHECK(visible_own > 1, "alamo", "human units in sight");

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
    static const char *const carriers[] = {"peasant-gold", "peasant-lumber", "peon-gold", "peon-lumber"};
    for (int i = 0; i < 4; ++i) {
        const cachedsprite_t *carrier = R_CacheFind(&cache, carriers[i]);
        RTS_CHECK(carrier && carrier->sprite.numlumps == 65 &&
                  carrier->sprite.spritedef.numframes == 13 &&
                  carrier->sprite.spritedef.spriteframes[0].rotations == 8, "alamo", "native carrier frames");
    }
    RTS_CHECK(tileset.tile_lookup[W2_TILE_LOOKUP] == 126, "alamo", "native removed-tree tile");

    P_FreeMobjList(&list);
    R_FreeSpriteCache(&cache);
    R_FreeSprite(&footman);
    R_FreeTileset(&tileset);
    D_QuitNetGame();
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
    blob_t file;
    RTS_CHECK(W_ReadFile("data/WAR2/CHANNEL.PUD", &file), "channel", "read movement map");
    int coasts = 0;
    for (size_t at = 0; at + 8 <= file.size;) {
        size_t len = read_u32_le(file.bytes + at + 4);
        RTS_CHECK(len <= file.size - at - 8, "channel", "section span");
        if (!memcmp(file.bytes + at, "SQM ", 4)) {
            for (int i = 0; i < level.width * level.height; ++i) {
                int sqm = read_u16_le(file.bytes + at + 8 + i * 2);
                if (sqm != 2 && sqm != 0x82) continue;
                ++coasts;
                RTS_CHECK(level.cell_terrain[i] == 3, "channel", "coast is not forest or land");
                for (int v = 0; v < level.resource_vent_count; ++v)
                    RTS_CHECK(!ivec2_equal(level.resource_vents[v].cell, (ivec2_t){i % level.width, i / level.width}),
                              "channel", "coast supplies no lumber");
            }
        }
        at += 8 + len;
    }
    W_FreeFile(&file);
    RTS_CHECK(coasts > 0 && coasts == count_terrain(3), "channel", "native coast count");
    RTS_CHECK(level.speeds->terrain[4][3] == 100 && level.speeds->terrain[2][3] == 0 &&
              level.speeds->terrain[1][3] == 0, "channel", "transport-only coast");
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
    RTS_CHECK(sheet_wh(&menu.panel[0][0], 0, 256, 288) &&
              sheet_wh(&menu.panel[1][0], 0, 256, 288), "ui", "panels");
    RTS_CHECK(menu.widgets[0].numlumps >= 18 && menu.widgets[1].numlumps >= 18 &&
              sheet_wh(&menu.widgets[0], 10, 106, 28) &&
              sheet_wh(&menu.widgets[0], 16, 224, 28) &&
              sheet_wh(&menu.widgets[1], 11, 106, 28) &&
              sheet_wh(&menu.widgets[1], 17, 224, 28), "ui", "widgets");
    RTS_CHECK(menu.font.glyph_index[(unsigned)'M'] >= 0 &&
              menu.font.glyph_width[(unsigned)'M'] == 14 && menu.font.glyph_size.h == 17 &&
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

static int test_campaign_records(void) {
    mkdir("build/test-user", 0777);
    setenv("OPEN_RTS_USER_DIR", "build/test-user", 1);
    G_InitGame();
    for (int number = 1; number <= W2_CAMPAIGN_LEVELS; ++number) {
        for (int orc = 0; orc < 2; ++orc) {
            char path[1200];
            RTS_CHECK(w2_extract_campaign_level("data/WAR2", number, orc, path, sizeof(path)), "campaign records", "extract");
            P_InitThinkers();
            W2_SetCampaign(number, orc);
            RTS_CHECK(G_DoLoadLevel(path, &level), "campaign records", "load");
            const w2_pud_t *pud = level.native_data;
            int expected = 0, troops = 0, workers = 0;
            for (int i = 0; i < pud->unit_count; ++i) {
                const w2_pud_unit_t *record = &pud->units[i];
                if (record->type >= W2_TYPE_COUNT) continue;
                const mobjinfo_t *info = &mobjinfo[record->type + 1];
                expected += info->name && !(info->w2.flags & W2_SKIP);
                troops += record->type == 0 || record->type == 1;
                workers += record->type == 2 || record->type == 3;
            }
            RTS_CHECK(P_LoadThings(NULL) == expected, "campaign records", "exact native spawn count");
            mobjlist_t units = P_ListMobjs();
            RTS_CHECK(units.count == expected, "campaign records", "no additional startup units");
            bool *matched = calloc((size_t)pud->unit_count, sizeof(bool));
            RTS_CHECK(matched, "campaign records", "allocate matches");
            for (int i = 0; i < units.count; ++i) {
                const mobj_t *unit = units.items[i];
                bool found = false;
                for (int j = 0; j < pud->unit_count; ++j) {
                    const w2_pud_unit_t *record = &pud->units[j];
                    if (matched[j] || unit->type_id != record->type + 1 || unit->owner != record->player) continue;
                    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
                    fvec2_t position = {record->x + foot.w * 0.5f, record->y + foot.h * 0.5f};
                    if (fvec2_distance_squared(fixed3_xy_to_fvec2(unit->core.position), position) != 0) continue;
                    matched[j] = found = true;
                    break;
                }
                RTS_CHECK(found, "campaign records", "unit type, owner and position match one native UNIT record");
            }
            if (number <= 2) fprintf(stderr, "campaign %d %s: records=%d spawned=%d infantry=%d workers=%d era=%d player=%d\n",
                number, orc ? "orc" : "human", pud->unit_count, units.count, troops, workers, pud->era, consoleplayer);
            free(matched);
            P_FreeMobjList(&units);
            P_FreeLevel(&level);
        }
    }
    puts("PASS: all 28 campaign maps spawn only native UNIT records, with exact types, owners and positions");
    /* A native terrain map with an empty UNIT section stays empty. The
     * driver smoke check also uses this fixture to exercise its startup. */
    blob_t file;
    RTS_CHECK(W_ReadFile("build/test-user/campaign-level01o.pud", &file), "empty map", "read native fixture");
    size_t at = 0;
    while (at + 8 <= file.size) {
        size_t length = read_u32_le(file.bytes + at + 4);
        RTS_CHECK(length <= file.size - at - 8, "empty map", "valid native section");
        if (!memcmp(file.bytes + at, "UNIT", 4)) {
            memmove(file.bytes + at + 8, file.bytes + at + 8 + length, file.size - at - 8 - length);
            memset(file.bytes + at + 4, 0, 4);
            file.size -= length;
            break;
        }
        at += 8 + length;
    }
    RTS_CHECK(at + 8 <= file.size, "empty map", "UNIT section found");
    const char *empty_path = "build/test-user/empty-campaign.pud";
    FILE *empty = fopen(empty_path, "wb");
    RTS_CHECK(empty && fwrite(file.bytes, 1, file.size, empty) == file.size, "empty map", "write fixture");
    RTS_CHECK(fclose(empty) == 0, "empty map", "close fixture");
    W_FreeFile(&file);
    W2_SetCampaign(0, false);
    P_InitThinkers();
    RTS_CHECK(G_DoLoadLevel(empty_path, &level) && P_LoadThings(NULL) == 0, "empty map", "no automatic units");
    RTS_CHECK(count_units() == 0, "empty map", "thinker list stays empty");
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    RTS_RUN(test_alamo());
    RTS_RUN(test_channel());
    RTS_RUN(test_ui_art());
    RTS_RUN(test_campaign_records());
    printf("warcraft-2 pud tests passed\n");
    return 0;
}
