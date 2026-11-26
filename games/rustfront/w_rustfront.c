#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RF_SPRITE_TABLES
#include "sprites.inc"

/* ── maps ───────────────────────────────────────────────────────────────────
 * A map is text: rows of cells, one terrain letter each (assets/terrain.txt: '.' sand, '*' ore,
 * '#' gravel, ',' grass, '^' stone, '~' water, 'R' rocks, 'x' wreck, '=' concrete), then one
 * line per thing,
 * "unit NAME OWNER X Y [FACING]" with X, Y the cell (a building's top-left) and FACING
 * 0..15 counter-clockwise from east. Owner 0 is the player; others are enemies, drawn in
 * their team colour (team = owner). A "camera X Y" line centres the opening view, and
 * "credits OWNER N" sets a side's money (5000 by default). A unit line may end in "@TAG", and
 * a campaign mission adds its own keys (g_campaign.c, docs/CAMPAIGN.md); the loader hands any
 * other line to RF_CampaignParseLine. Every ore cell is a deposit of
 * ORE_PER_CELL that a harvester draws ORE_RATE from a second. */

#define ORE_PER_CELL 1000
#define ORE_RATE 50
#define START_CREDITS 5000

#define RF_MAX_THINGS 256

typedef struct { uint16_t type; int owner, facing; ivec2_t cell; char tag[16]; } rfthing_t;
static rfthing_t things[RF_MAX_THINGS];
static int thing_count;

static const struct { const char *name; uint16_t type; } unit_names[] = {
    {"hq", MT_HQ}, {"power", MT_POWER}, {"refinery", MT_REFINERY}, {"barracks", MT_BARRACKS},
    {"factory", MT_FACTORY}, {"turret", MT_GUN_TURRET}, {"scout", MT_SCOUT}, {"tank", MT_TANK},
    {"artillery", MT_ARTILLERY}, {"harvester", MT_HARVESTER}, {"apc", MT_APC},
    {"rifleman", MT_RIFLEMAN}, {"rocket", MT_ROCKET}, {"mastodon", MT_MASTODON},
    {"cl_hall", MT_CL_HALL}, {"cl_generator", MT_CL_GENERATOR}, {"cl_smelter", MT_CL_SMELTER},
    {"cl_camp", MT_CL_CAMP}, {"cl_yard", MT_CL_YARD}, {"cl_tower", MT_CL_TOWER}, {"cl_trike", MT_CL_TRIKE},
    {"cl_truck", MT_CL_TRUCK}, {"cl_crawler", MT_CL_CRAWLER}, {"cl_rig", MT_CL_RIG},
    {"cl_hauler", MT_CL_HAULER}, {"cl_raider", MT_CL_RAIDER}, {"cl_torcher", MT_CL_TORCHER},
    {"lc_spire", MT_LC_SPIRE}, {"lc_fusion", MT_LC_FUSION}, {"lc_extractor", MT_LC_EXTRACTOR},
    {"lc_academy", MT_LC_ACADEMY}, {"lc_foundry", MT_LC_FOUNDRY}, {"lc_tower", MT_LC_TOWER},
    {"lc_skimmer", MT_LC_SKIMMER}, {"lc_glaive", MT_LC_GLAIVE}, {"lc_prism", MT_LC_PRISM},
    {"lc_harvester", MT_LC_HARVESTER}, {"lc_warden", MT_LC_WARDEN}, {"lc_lancer", MT_LC_LANCER},
    {"uplink", MT_UPLINK}, {"hammer", MT_HAMMER}, {"cl_shop", MT_CL_SHOP}, {"cl_wrecker", MT_CL_WRECKER},
    {"lc_sanctum", MT_LC_SANCTUM}, {"lc_aegis", MT_LC_AEGIS},
    {"crane", MT_CRANE}, {"phantom", MT_PHANTOM}, {"kestrel", MT_KESTREL}, {"sentry_truck", MT_SENTRY_TRUCK},
    {"sentry", MT_SENTRY}, {"cl_drill", MT_CL_DRILL}, {"cl_derrick", MT_CL_DERRICK}, {"cl_sapper", MT_CL_SAPPER},
    {"cl_mine", MT_CL_MINE}, {"cl_crusher", MT_CL_CRUSHER}, {"cl_buzzard", MT_CL_BUZZARD},
    {"lc_strider", MT_LC_STRIDER}, {"lc_seraph", MT_LC_SERAPH}, {"lc_cantor", MT_LC_CANTOR},
};

uint16_t RF_UnitType(const char *name) {
    for (size_t i = 0; i < sizeof(unit_names) / sizeof(*unit_names); ++i)
        if (!strcmp(unit_names[i].name, name)) return unit_names[i].type;
    return 0;
}

static bool grid_line(const char *line, int len) {
    if (len <= 0) return false;
    for (int i = 0; i < len; ++i)
        if (RF_TerrainOfLetter(line[i]) < 0) return false;
    return true;
}

static bool add_ore(level_t *map, ivec2_t cell) {
    resourcevent_t *vents = realloc(map->resource_vents, (size_t)(map->resource_vent_count + 1) * sizeof(*vents));
    if (!vents) return false;
    map->resource_vents = vents;
    vents[map->resource_vent_count++] = (resourcevent_t){
        .cell = cell, .attachment = fixed2_cell_center(cell), .amount = ORE_PER_CELL,
        .rate = ORE_RATE, .active = true, .resource_type = 0};
    return true;
}

/* An ore cell that has been mined out turns back to the default ground (sand). */
void RF_ClearMinedOre(level_t *map) {
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (P_VentYields(vent) || !L_Contains(map, vent->cell.x, vent->cell.y)) continue;
        int cell = L_Index(map, vent->cell.x, vent->cell.y);
        if (map->cell_terrain && RF_TerrainHoldsOre(map->cell_terrain[cell]))
            RF_SetCellTerrain(map, vent->cell.x, vent->cell.y, RF_DefaultTerrain());
    }
}

bool RF_LoadLevel(const char *path, level_t *out) {
    RF_UnitsReset();
    blob_t file;
    if (!W_ReadFile(path, &file)) {
        fprintf(stderr, "rustfront: cannot read map %s\n", path);
        return false;
    }
    memset(out, 0, sizeof(*out));
    thing_count = 0;
    RF_ClearStarts();
    RF_CampaignBeginLoad();
    if (!RF_LoadTerrain(path)) return false;
    char *text = malloc(file.size + 1);
    if (!text) { W_FreeFile(&file); return false; }
    memcpy(text, file.bytes, file.size);
    text[file.size] = '\0';
    W_FreeFile(&file);

    /* First pass: the grid's size. */
    int w = 0, h = 0;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        if (len && line[len - 1] == '\r') len--;
        if (grid_line(line, len)) {
            if (w && len != w) {
                fprintf(stderr, "rustfront: %s row %d is %d cells, not %d\n", path, h + 1, len, w);
                free(text);
                return false;
            }
            w = len;
            h++;
        }
        line = end ? end + 1 : line + strlen(line);
    }
    if (!w || !h) {
        fprintf(stderr, "rustfront: %s has no cell rows\n", path);
        free(text);
        return false;
    }
    out->width = w;
    out->height = h;
    out->tile_ids = calloc((size_t)w * h, sizeof(*out->tile_ids));
    out->blocked = calloc((size_t)w * h, 1);
    out->cell_terrain = calloc((size_t)w * h, 1);
    if (!out->tile_ids || !out->blocked || !out->cell_terrain || !RF_BeginRelief(out)) {
        free(text);
        P_FreeLevel(out);
        return false;
    }
    out->render_capabilities |= MAP_RENDER_CAP_TERRAIN_TRANSITIONS;
    out->render_transitions = RF_DrawTransitions;
    snprintf(out->tileset_name, sizeof(out->tileset_name), "terrain");
    for (int owner = 0; owner < MAXPLAYERS; ++owner) out->player_resources[owner][0] = START_CREDITS;

    /* Second pass: cells, levels and things. */
    int y = 0, level_row = 0;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        if (len && line[len - 1] == '\r') len--;
        if (grid_line(line, len)) {
            for (int x = 0; x < w; ++x) {
                int terrain = RF_TerrainOfLetter(line[x]);
                RF_SetCellTerrain(out, x, y, terrain);
                out->blocked[L_Index(out, x, y)] = RF_TerrainBlocks(terrain);
                if (RF_TerrainHoldsOre(terrain) && !add_ore(out, (ivec2_t){x, y})) {
                    free(text);
                    P_FreeLevel(out);
                    return false;
                }
            }
            y++;
        } else if (!strncmp(line, "level ", 6)) {
            /* a row of levels: digits, '/' for a ramp */
            if (len - 6 != w || level_row >= h) {
                fprintf(stderr, "rustfront: %s level row %d is not %d cells\n", path, level_row + 1, w);
                free(text);
                P_FreeLevel(out);
                return false;
            }
            for (int x = 0; x < w; ++x) {
                char c = line[6 + x];
                RF_SetCellLevel(out, x, level_row, c >= '0' && c <= '9' ? c - '0' : 0, c == '/');
            }
            level_row++;
        } else {
            char name[24];
            int owner, x, cy, facing = 0;
            line[len] = '\0';
            if (!strncmp(line, "start ", 6)) {
                RF_ParseStart(line, w, h);
            } else if (sscanf(line, "unit %23s %d %d %d %d", name, &owner, &x, &cy, &facing) >= 4) {
                uint16_t type = RF_UnitType(name);
                if (!type || x < 0 || cy < 0 || x >= w || cy >= h || owner < 0 || owner >= MAXPLAYERS) {
                    fprintf(stderr, "rustfront: %s: bad unit line \"%.*s\"\n", path, len, line);
                } else if (thing_count < RF_MAX_THINGS) {
                    /* "unit ... @TAG" names the thing for a mission's objectives (g_campaign.c). */
                    rfthing_t *t = &things[thing_count++];
                    *t = (rfthing_t){type, owner, facing & 15, {x, cy}, ""};
                    const char *at = memchr(line, '@', (size_t)len);
                    if (at) {
                        int n = 0;
                        while (n < (int)sizeof(t->tag) - 1 && at + 1 + n < line + len && !isspace((unsigned char)at[1 + n]))
                            n++;
                        memcpy(t->tag, at + 1, (size_t)n);
                        t->tag[n] = '\0';
                    }
                }
            } else if (sscanf(line, "credits %d %d", &owner, &x) == 2) {
                if (owner >= 0 && owner < MAXPLAYERS) out->player_resources[owner][0] = x;
            } else if (sscanf(line, "camera %d %d", &x, &cy) == 2) {
                out->has_camera = true;
                out->camera = (fvec2_t){(float)x, (float)cy};
            } else {
                char copy[512];
                snprintf(copy, sizeof(copy), "%.*s", len, line);
                RF_CampaignParseLine(copy, w, h);
            }
        }
        line = end ? end + 1 : line + strlen(line);
    }
    free(text);
    RF_CampaignEndLoad(path);
    RF_BuildRelief(out);
    for (int i = 0; i < w * h; ++i) out->blocked[i] = RF_CellBlocks(out, i);
    /* In a network game every side is its own, for the computer players too (p_ai.c asks
     * player_teams, where P_IsAlly asks netgame); a map with starts is always free-for-all. */
    out->player_teams = netgame || RF_StartCount() > 0;
    return true;
}

/* A unit, or a structure with its top-left at `cell`, in its owner's team colour. */
mobj_t *RF_SpawnThing(uint16_t type_id, int owner, int facing, ivec2_t cell) {
    const mobjtype_t *type = P_ActorType(type_id);
    if (!type || owner < 0 || owner >= MAXPLAYERS) return NULL;
    mobj_t *mo = P_SpawnMobj(fixed3_zero(), type_id);
    if (!mo) return NULL;
    bool building = type->footprint.w > 0;
    fixed2_t at = building ? P_BuildingPosition(type_id, cell) : fixed2_cell_center(cell);
    mo->core.position = fixed3_from_fixed2(at, 0);
    mo->core.angle = (angle_t)(facing & 15) * (ANG90 / 4);
    mo->owner = (uint8_t)owner;
    mo->team = (uint8_t)owner;
    mo->allegiance = owner == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    if (building)
        for (int dy = 0; dy < type->footprint.h; ++dy)
            for (int dx = 0; dx < type->footprint.w; ++dx) {
                int x = cell.x + dx, y = cell.y + dy;
                if (L_Contains(&level, x, y)) level.blocked[L_Index(&level, x, y)] = 1;
            }
    return mo;
}

int RF_LoadThings(const char *path) {
    (void)path; /* Parsed by RF_LoadLevel. */
    int count = 0;
    for (int i = 0; i < thing_count; ++i) {
        const rfthing_t *t = &things[i];
        mobj_t *mo = RF_SpawnThing(t->type, t->owner, t->facing, t->cell);
        if (!mo) continue;
        count++;
        if (t->tag[0]) RF_CampaignTagged(t->tag, mo);
    }
    return count + RF_SpawnStarts();
}

/* ── terrain tiles ─────────────────────────────────────────────────────────
 * assets/terrain.pcx is an atlas of 48 x 32 tiles in the game palette (art/terrain.py,
 * pack8.py), which becomes the screen palette; assets/terrain.txt says which tile is which.
 * Index 0 is the sprites' transparent key: ground tiles never use it, and transition tiles
 * leave the ground showing through it. */

bool RF_LoadAssets(const char *root, tileset_t *tileset) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/assets/terrain.pcx", root);
    spritesheet_t atlas;
    if (!W_LoadIndexedSheet(path, &atlas)) {
        fprintf(stderr, "rustfront: cannot load %s\n", path);
        return false;
    }
    int aw = atlas.cells[0].rect.w, ah = atlas.cells[0].rect.h;
    int cols = aw / RF_TILE_W, rows = ah / RF_TILE_H, count = cols * rows;
    memset(tileset, 0, sizeof(*tileset));
    tileset->indices = malloc((size_t)count * RF_TILE_W * RF_TILE_H);
    if (!count || !tileset->indices) { R_FreeSprite(&atlas); return false; }
    for (int t = 0; t < count; ++t)
        for (int y = 0; y < RF_TILE_H; ++y)
            memcpy(tileset->indices + ((size_t)t * RF_TILE_H + y) * RF_TILE_W,
                   atlas.lumps[0].indices + ((size_t)(t / cols) * RF_TILE_H + y) * aw + (t % cols) * RF_TILE_W,
                   RF_TILE_W);
    memcpy(tileset->palette, atlas.source_palette, sizeof(tileset->palette));
    tileset->palette[0] = 0xff000000u;
    tileset->count = count;
    tileset->atlas_cols = cols;
    tileset->tile_w = RF_TILE_W;
    tileset->tile_h = RF_TILE_H;
    R_FreeSprite(&atlas);
    return true;
}

/* ── sprites ───────────────────────────────────────────────────────────────
 * Facings in the art run counter-clockwise from east (art/sprites.py); the renderer's
 * rotation slots run counter-clockwise from north, so slot s shows facing s + n/4. */

static int slot_for(int facing, int n) {
    return ((facing - n / 4) % n + n) % n;
}

/* The tight box of a lump's opaque pixels: picking, selection and health bars use it. */
static irect_t opaque_bounds(const uint8_t *px, int w, int h) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (px[y * w + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    return x1 < 0 ? (irect_t){0, 0, w, h} : (irect_t){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

/* Team colour: art is drawn in the blue ramp at RF_TEAM_BASE; team t (1..3) moves it to ramp t. */
static bool install_palette(spritesheet_t *sheet, const uint32_t source[256], bool shadow) {
    memcpy(sheet->palette, source, sizeof(sheet->palette));
    sheet->palette[0] = 0;
    /* Half-alpha black: ground shadows, the sprites' soft contact edge, and smoke. */
    sheet->palette[RF_SHADOW_INDEX] = 0x80000000u;
    memcpy(sheet->source_palette, sheet->palette, sizeof(sheet->palette));
    sheet->indexed = true;
    if (shadow) return true;
    sheet->palette_maps = calloc(RF_TEAM_COUNT - 1, sizeof(*sheet->palette_maps));
    if (!sheet->palette_maps) return false;
    sheet->palette_map_count = RF_TEAM_COUNT - 1;
    for (int team = 1; team < RF_TEAM_COUNT; ++team) {
        spritepalettemap_t *map = &sheet->palette_maps[team - 1];
        map->id = team;
        for (int i = 0; i < 256; ++i) map->indices[i] = (uint8_t)i;
        for (int i = 0; i < RF_TEAM_SHADES; ++i)
            map->indices[RF_TEAM_BASE + i] = (uint8_t)(RF_TEAM_BASE + team * RF_TEAM_SHADES + i);
    }
    return true;
}

/* Logical frame and rotation slot of lump `lump` in a sheet of `def`'s unit. Infantry sheets
 * hold every animation (rf_anims); the rest hold one picture per facing. */
static void lump_place(const rf_sheetdef_t *def, int lump, int *frame, int *slot, int *frames, int *rotations) {
    int first = 0;
    for (size_t i = 0; i < sizeof(rf_anims) / sizeof(*rf_anims); ++i) {
        const rf_animdef_t *a = &rf_anims[i];
        if (strcmp(a->unit, def->unit)) continue;
        int span = a->frames * a->facings;
        if (lump >= a->start && lump < a->start + span) {
            int off = lump - a->start;
            *frame = first + off % a->frames;
            *slot = slot_for(off / a->frames, a->facings);
            *rotations = a->facings;
        }
        first += a->frames;
        *frames = first;
    }
    if (first) return;
    *frames = def->frames;
    *frame = lump % def->frames;
    *rotations = def->count / def->frames;
    *slot = slot_for(lump / def->frames, *rotations);
}

static bool load_sheet(const char *root, const rf_sheetdef_t *def, spritesheet_t *sheet) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/assets/%s", root, def->file);
    spritesheet_t image;
    if (!W_LoadIndexedSheet(path, &image)) {
        fprintf(stderr, "rustfront: cannot load %s\n", path);
        return false;
    }
    int iw = image.cells[0].rect.w, ih = image.cells[0].rect.h;
    if (iw < def->w * def->columns || ih < def->h * ((def->count + def->columns - 1) / def->columns)) {
        fprintf(stderr, "rustfront: %s is %dx%d, too small for its sprites.inc grid\n", path, iw, ih);
        R_FreeSprite(&image);
        return false;
    }
    int frames = 1, rotations = 1, frame, slot;
    lump_place(def, 0, &frame, &slot, &frames, &rotations);
    bool ok = R_AllocSpriteCells(sheet, def->count) && R_InitSpriteDef(sheet, frames, rotations);
    for (int lump = 0; ok && lump < def->count; ++lump) {
        uint8_t *px = malloc((size_t)def->w * def->h);
        if (!px) { ok = false; break; }
        int col = lump % def->columns, row = lump / def->columns;
        for (int y = 0; y < def->h; ++y)
            memcpy(px + (size_t)y * def->w,
                   image.lumps[0].indices + ((size_t)row * def->h + y) * iw + (size_t)col * def->w, def->w);
        sheet->lumps[lump].indices = px;
        spritecell_t *cell = &sheet->cells[lump];
        cell->rect = (irect_t){0, 0, def->w, def->h};
        cell->bounds = opaque_bounds(px, def->w, def->h);
        cell->ground_point = (ivec2_t){def->pivot_x, def->pivot_y};
        lump_place(def, lump, &frame, &slot, &frames, &rotations);
        ok = R_InstallSpriteLump(sheet, frame, slot, lump, false);
    }
    sheet->frame_size = (isize2_t){def->w, def->h};
    ok = ok && install_palette(sheet, image.source_palette, !strcmp(def->layer, "shadow"));
    R_FreeSprite(&image);
    return ok;
}

/* A hull's mount points: where its turret's ground point sits in each hull picture. */
static void set_mounts(spritesheet_t *hull, const rf_sheetdef_t *hulldef, const rf_sheetdef_t *turret) {
    for (int lump = 0; lump < hull->numlumps; ++lump) {
        const short *m = turret->mount[lump / hulldef->frames];
        hull->cells[lump].mount = (ivec2_t){hulldef->pivot_x + m[0], hulldef->pivot_y + m[1]};
    }
}

bool RF_LoadSprites(const char *root, spritecache_t *cache) {
    bool ok = true;
    for (size_t i = 0; i < sizeof(rf_sheets) / sizeof(*rf_sheets); ++i) {
        const rf_sheetdef_t *def = &rf_sheets[i];
        char name[32];
        snprintf(name, sizeof(name), "%s_%s", def->unit, def->layer);
        if (R_CacheFind(cache, name)) continue;
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *entry = &cache->entries[cache->count];
        memset(entry, 0, sizeof(*entry));
        if (!load_sheet(root, def, &entry->sprite)) {
            R_FreeSprite(&entry->sprite);
            ok = false;
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        cache->count++;
    }
    /* Turret layers mount on their unit's first layer. */
    for (size_t i = 0; i < sizeof(rf_sheets) / sizeof(*rf_sheets); ++i) {
        const rf_sheetdef_t *def = &rf_sheets[i];
        if (!def->mount) continue;
        const rf_sheetdef_t *hulldef = NULL;
        for (size_t j = 0; j < sizeof(rf_sheets) / sizeof(*rf_sheets) && !hulldef; ++j)
            if (!strcmp(rf_sheets[j].unit, def->unit) && !rf_sheets[j].mount &&
                strcmp(rf_sheets[j].layer, "shadow")) hulldef = &rf_sheets[j];
        char name[32];
        if (!hulldef) continue;
        snprintf(name, sizeof(name), "%s_%s", hulldef->unit, hulldef->layer);
        cachedsprite_t *hull = R_CacheFind(cache, name);
        if (hull) set_mounts(&hull->sprite, hulldef, def);
    }
    return R_BindSprites(cache, gameinfo) && ok;
}
