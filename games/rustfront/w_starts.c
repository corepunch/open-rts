#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── start positions ────────────────────────────────────────────────────────
 * A multiplayer map has no bases of its own. Its "start N X Y [FACTION]" lines mark where a
 * side may begin; whoever sets up the game (the lobby, or the defaults below) says which
 * owner starts where and as which faction, and RF_LoadThings spawns each side's start with
 * RF_SpawnStart. Everything here depends only on the map and the plan, never on the local
 * player, so every lockstep peer builds the same world. */

static rf_start_t starts[RF_MAX_STARTS];
static int start_count;
static rf_startplan_t plan;
static bool have_plan;

void RF_ClearStarts(void) {
    memset(starts, 0, sizeof(starts));
    start_count = 0;
}

int RF_FactionByName(const char *name) {
    static const char *const names[RF_FACTION_COUNT] = {"directorate", "clans", "concord"};
    for (int f = 0; name && f < RF_FACTION_COUNT; ++f)
        if (!strcmp(name, names[f])) return f;
    return -1;
}

/* "start N X Y [FACTION]": start N (0-based) at cell X, Y, the top-left of its headquarters. */
bool RF_ParseStart(const char *line, int width, int height) {
    int slot, x, y;
    char faction[24] = "";
    if (sscanf(line, "start %d %d %d %23s", &slot, &x, &y, faction) < 3) return false;
    if (slot < 0 || slot >= RF_MAX_STARTS || x < 0 || y < 0 || x + 3 > width || y + 3 > height || starts[slot].used) {
        fprintf(stderr, "rustfront: bad or repeated start line \"%s\"\n", line);
        return true;
    }
    int f = faction[0] ? RF_FactionByName(faction) : -1;
    starts[slot] = (rf_start_t){.used = true, .cell = {x, y}, .faction = f >= 0 ? f : slot % RF_FACTION_COUNT};
    if (slot + 1 > start_count) start_count = slot + 1;
    return true;
}

int RF_StartCount(void) {
    for (int i = 0; i < start_count; ++i)
        if (!starts[i].used) return i; /* starts are numbered without gaps */
    return start_count;
}

const rf_start_t *RF_Start(int slot) {
    return slot >= 0 && slot < RF_StartCount() ? &starts[slot] : NULL;
}

void RF_SetStartPlan(const rf_startplan_t *p) {
    have_plan = p != NULL;
    plan = p ? *p : (rf_startplan_t){0};
}

/* ── one side's start ──────────────────────────────────────────────────── */

/* Each faction's starting force beside its headquarters: a harvester for the refinery to
 * come, a light vehicle and a squad, worth about the same. */
static const struct { int faction; uint16_t types[8]; } FORCES[] = {
    {RF_DIRECTORATE, {MT_HARVESTER, MT_SCOUT, MT_RIFLEMAN, MT_RIFLEMAN, MT_RIFLEMAN, MT_ROCKET}},
    {RF_CLANS, {MT_CL_HAULER, MT_CL_TRIKE, MT_CL_TRIKE, MT_CL_RAIDER, MT_CL_RAIDER, MT_CL_RAIDER, MT_CL_RAIDER,
                MT_CL_RAIDER}},
    {RF_CONCORD, {MT_LC_HARVESTER, MT_LC_SKIMMER, MT_LC_WARDEN, MT_LC_WARDEN, MT_LC_LANCER}},
};

/* The 16 facings (counter-clockwise from east, the screen's y pointing south) as unit
 * vectors times 1000, so the start can face the middle with integer maths alone. */
static const int16_t FACING_DIR[16][2] = {
    {1000, 0}, {924, -383}, {707, -707}, {383, -924}, {0, -1000}, {-383, -924}, {-707, -707}, {-924, -383},
    {-1000, 0}, {-924, 383}, {-707, 707}, {-383, 924}, {0, 1000}, {383, 924}, {707, 707}, {924, 383},
};

static int facing_toward(ivec2_t from, ivec2_t to) {
    int64_t dx = to.x - from.x, dy = to.y - from.y, best_dot = INT64_MIN;
    int best = 0;
    for (int f = 0; f < 16; ++f) {
        int64_t dot = dx * FACING_DIR[f][0] + dy * FACING_DIR[f][1];
        if (dot > best_dot) { best_dot = dot; best = f; }
    }
    return best;
}

static bool cell_free(ivec2_t c, const uint8_t *taken, int level_of_hq) {
    if (!L_Contains(&level, c.x, c.y)) return false;
    int i = L_Index(&level, c.x, c.y);
    return !level.blocked[i] && !taken[i] && RF_CellRelief(&level, c.x, c.y) == RF_RELIEF_FLAT &&
           RF_CellLevel(&level, c.x, c.y) == level_of_hq;
}

/* The free cell nearest `want` (ring by ring, in a fixed order) on the headquarters' level. */
static bool nearest_free(ivec2_t want, const uint8_t *taken, int level_of_hq, ivec2_t *out) {
    for (int r = 0; r < 8; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (abs(dx) != r && abs(dy) != r) continue;
                ivec2_t c = {want.x + dx, want.y + dy};
                if (cell_free(c, taken, level_of_hq)) { *out = c; return true; }
            }
    return false;
}

bool RF_SpawnStart(int owner, int faction, int x, int y) {
    uint16_t hq_type = RF_Role(faction, RF_ROLE_HQ);
    const mobjtype_t *hq_info = P_ActorType(hq_type);
    if (owner < 0 || owner >= MAXPLAYERS || !hq_info || !level.blocked) return false;
    isize2_t foot = hq_info->footprint;
    int hq_level = RF_CellLevel(&level, x, y);
    for (int cy = y; cy < y + foot.h; ++cy)
        for (int cx = x; cx < x + foot.w; ++cx)
            if (!L_Contains(&level, cx, cy) || level.blocked[L_Index(&level, cx, cy)] ||
                RF_CellRelief(&level, cx, cy) != RF_RELIEF_FLAT || RF_CellLevel(&level, cx, cy) != hq_level) {
                fprintf(stderr, "rustfront: start at %d %d has no flat, open ground for its headquarters\n", x, y);
                return false;
            }
    ivec2_t centre = {level.width / 2, level.height / 2};
    int facing = facing_toward((ivec2_t){x + 1, y + 1}, centre);
    if (!RF_SpawnThing(hq_type, owner, facing, (ivec2_t){x, y})) return false;

    /* The force stands on the headquarters' side toward the middle, in front of its door. */
    uint8_t *taken = calloc((size_t)level.width * level.height, 1);
    if (!taken) return false;
    int fdx = FACING_DIR[facing][0], fdy = FACING_DIR[facing][1];
    ivec2_t front = {x + 1 + (fdx > 382 ? 3 : fdx < -382 ? -3 : 0), y + 1 + (fdy > 382 ? 3 : fdy < -382 ? -3 : 0)};
    if (front.x == x + 1 && front.y == y + 1) front.y += 3;
    int f = 0;
    while (f < (int)(sizeof(FORCES) / sizeof(*FORCES)) && FORCES[f].faction != faction) ++f;
    for (int i = 0; f < (int)(sizeof(FORCES) / sizeof(*FORCES)) && i < 8 && FORCES[f].types[i]; ++i) {
        int row = i / 3, col = i % 3 - 1;
        ivec2_t want = abs(fdy) >= abs(fdx) ? (ivec2_t){front.x + col, front.y + (fdy > 0 ? row : -row)}
                                            : (ivec2_t){front.x + (fdx > 0 ? row : -row), front.y + col}, at;
        if (!nearest_free(want, taken, hq_level, &at)) continue;
        taken[L_Index(&level, at.x, at.y)] = 1;
        RF_SpawnThing(FORCES[f].types[i], owner, facing, at);
    }
    free(taken);
    return true;
}

/* Spawns every side the plan names, or with no plan, every start as its default faction, and
 * returns how many things that made. Called by RF_LoadThings once the map's own units are down. */
int RF_SpawnStarts(void) {
    int count = RF_StartCount();
    uint32_t first_id = level.next_mobj_id;
    if (!count) return 0;
    rf_startplan_t p = plan;
    if (!have_plan) {
        p = (rf_startplan_t){.count = count};
        for (int i = 0; i < count; ++i) p.seat[i] = (rf_seat_t){.start = (int8_t)i, .faction = -1};
    }
    bool used[RF_MAX_STARTS] = {0};
    /* Free for all, except seats that share a team. */
    level.player_teams = true;
    for (int owner = 0; owner < p.count && owner < RF_MAX_STARTS; ++owner) {
        const rf_seat_t *seat = &p.seat[owner];
        if (seat->start < 0 || seat->start >= count || used[seat->start]) continue;
        used[seat->start] = true;
        const rf_start_t *s = &starts[seat->start];
        int faction = seat->faction >= 0 && seat->faction < RF_FACTION_COUNT ? seat->faction : s->faction;
        if (!RF_SpawnStart(owner, faction, s->cell.x, s->cell.y)) continue;
        level.sight.allies[owner] |= UINT32_C(0x40000000) >> owner;
        for (int other = 0; seat->team && other < p.count && other < RF_MAX_STARTS; ++other)
            if (other != owner && p.seat[other].team == seat->team)
                level.sight.allies[owner] |= UINT32_C(0x40000000) >> other;
        if (owner == consoleplayer) { /* only the view: not part of the simulation */
            level.has_camera = true;
            level.camera = (fvec2_t){(float)s->cell.x + 1.5f, (float)s->cell.y + 1.5f};
        }
    }
    RF_SyncBlocking(&level);
    return (int)(level.next_mobj_id - first_id);
}

/* ── the map list ──────────────────────────────────────────────────────── */

/* Reads a map's header: its "title" line, its size and its starts, without loading it. */
static bool read_info(const char *root, const char *name, rf_mapinfo_t *info) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/maps/%s", root, name);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    *info = (rf_mapinfo_t){0};
    snprintf(info->path, sizeof(info->path), "maps/%s", name);
    snprintf(info->title, sizeof(info->title), "%.*s", (int)(strlen(name) - 4), name);
    char line[512];
    uint8_t owners = 0;
    bool seen[RF_MAX_STARTS] = {0};
    while (fgets(line, sizeof(line), file)) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        int slot, owner, x, y;
        char unit[24];
        if (!strncmp(line, "title ", 6)) {
            snprintf(info->title, sizeof(info->title), "%s", line + 6);
        } else if (sscanf(line, "start %d %d %d", &slot, &x, &y) == 3) {
            if (slot >= 0 && slot < RF_MAX_STARTS && !seen[slot]) { seen[slot] = true; info->players++; }
        } else if (sscanf(line, "unit %23s %d %d %d", unit, &owner, &x, &y) == 4) {
            if (owner >= 0 && owner < 8) owners |= (uint8_t)(1u << owner);
        } else if (len && line[0] != '#' && !strchr(line, ' ')) { /* a row of cells */
            if (!info->width) info->width = (int)len;
            info->height++;
        }
    }
    fclose(file);
    for (int p = 0; p < 8; ++p) info->sides += (owners >> p) & 1;
    return info->width > 0;
}

static int by_players_then_title(const void *a, const void *b) {
    const rf_mapinfo_t *x = a, *y = b;
    if (x->players != y->players) return x->players - y->players;
    int t = strcmp(x->title, y->title);
    return t ? t : strcmp(x->path, y->path);
}

int RF_ListMaps(const char *root, rf_mapinfo_t *out, int max) {
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/maps", root ? root : g_game_default_root);
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;
    int count = 0;
    for (struct dirent *e; (e = readdir(dir)) && count < max;) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".txt")) continue;
        if (read_info(root ? root : g_game_default_root, e->d_name, &out[count])) count++;
    }
    closedir(dir);
    qsort(out, (size_t)count, sizeof(*out), by_players_then_title);
    return count;
}
