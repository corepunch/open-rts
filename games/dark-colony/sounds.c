/* Dark Colony sounds, after DC.EXE sound.c (0x42d91c..0x42f198).
 *
 * SOUND/SOUND2.DAT lists the 200 sound objects: index, path, buffer count,
 * an always-1 field, DirectSound volume in centibels and a loop flag. Object
 * n becomes sfx n + 1.
 *
 * SOUND/SLIST.DAT gives each native object type (or weapon sound value, or
 * explosion type) up to ten objects per category, at 13 bytes per entry in
 * the 0x4c6c54 table: count, cursor, bark priority, ids. Every entry becomes
 * an engine sfx group.
 *
 * SOUND/<tileset>.AMB lists ambience by time of day and MAP block type
 * (0x42e6ac). Every 5 s and every 7 s (0x42ee98) the most common visible
 * block type in the view (0x441c04) plays its next ambience. */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "gamestat.h"
#include <ctype.h>
#include <math.h>

enum {
    DC_NUMSOUNDS = 200,      /* NUM_SOUND_OBJECTS. */
    DC_NUMSLISTIDS = 200,
    DC_NUMBLOCKTYPES = 33,   /* "blocktype >= 0 && blocktype <= 32". */
    DC_NUMTILES = 2048,
    DC_AMBIENT_A_MS = 5000,
    DC_AMBIENT_B_MS = 7000,
    DC_ALERT_MS = 30000,
};

typedef enum { CAT_GUN, CAT_ACK, CAT_SEL, CAT_DEA, CAT_AMB, CAT_DPY, CAT_EXP, CAT_XTR,
               NUMCATEGORIES } category_t;

static const char *const categorynames[NUMCATEGORIES] = {
    "GUN", "ACK", "SEL", "DEA", "AMB", "DPY", "EXP", "XTR",
};

#define DC_SFX(object) ((object) + 1)
enum {
    SND_UNIT = 0,     /* UNIT.WAV: a unit is under attack. */
    SND_DROPLP = 45,
    SND_DROPLPG = 82,
    SND_BUTTON = 97,
    SND_BASE = 118,   /* BASE.WAV: the base is under attack. */
    SND_HUM = 134,
    SND_ACTIVE = 186,
    SND_MSG = 187,
};

static int slist[DC_NUMSLISTIDS][NUMCATEGORIES];
static int ambience[DC_NUMBLOCKTYPES][2]; /* Preallocated groups, refilled per level. */
static uint8_t blocktypes[DC_NUMTILES];
static uint32_t ambient_a, ambient_b, alert_unit, alert_base;

static char *next_line(char **cursor) {
    char *line = *cursor;
    if (!line || !*line) return NULL;
    char *end = strchr(line, '\n');
    if (end) { *end = '\0'; *cursor = end + 1; }
    else *cursor = line + strlen(line);
    char *cr = strchr(line, '\r');
    if (cr) *cr = '\0';
    return line;
}

static bool read_text(const char *root, const char *name, blob_t *out) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return W_ReadFile(path, out);
}

/* ".\SOUND\UNIT.WAV" -> "SOUND/UNIT.WAV". */
static void native_path(char *dst, size_t size, const char *src) {
    if (src[0] == '.' && (src[1] == '\\' || src[1] == '/')) src += 2;
    size_t i = 0;
    for (; src[i] && i + 1 < size; ++i)
        dst[i] = src[i] == '\\' ? '/' : (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static bool load_sound2(const char *root) {
    blob_t file;
    if (!read_text(root, "SOUND/SOUND2.DAT", &file)) return false;
    for (int i = 0; i < DC_NUMSOUNDS; ++i)
        if (S_AddSfx(&(sfxinfo_t){0}) != DC_SFX(i)) { W_FreeFile(&file); return false; }
    char *cursor = (char *)file.bytes, *line;
    while ((line = next_line(&cursor)) != NULL) {
        if (line[0] == '*') break;
        int index, count, field, volume, loop;
        char name[256];
        if (sscanf(line, "%d %255s %d %d %d %d", &index, name, &count, &field, &volume, &loop) != 6 ||
            index < 0 || index >= DC_NUMSOUNDS) continue;
        sfxinfo_t *sfx = S_Sfx(DC_SFX(index));
        native_path(sfx->name, sizeof(sfx->name), name);
        sfx->instances = count > 0 ? count : 1;
        sfx->volume = volume < 0 ? volume : 0;
        sfx->loop = loop == 1;
    }
    W_FreeFile(&file);
    return true;
}

/* Reads "id id ... -1" into a group; returns the text after the -1. */
static const char *read_group(const char *text, sfxinfo_t *group) {
    group->numlinks = 0;
    for (;;) {
        char *end;
        long value = strtol(text, &end, 10);
        if (end == text || value == -1) return end == text ? text : end;
        text = end;
        if (value >= 0 && value < DC_NUMSOUNDS && group->numlinks < MAXSFXLINKS)
            group->links[group->numlinks++] = (int16_t)DC_SFX(value);
    }
}

static bool load_slist(const char *root) {
    blob_t file;
    if (!read_text(root, "SOUND/SLIST.DAT", &file)) return false;
    char *cursor = (char *)file.bytes, *line;
    while ((line = next_line(&cursor)) != NULL) {
        if (line[0] == '%' || strlen(line) < 3) continue;
        char *end;
        long id = strtol(line, &end, 10);
        if (end == line || id < 0 || id >= DC_NUMSLISTIDS) continue;
        char name[8];
        int consumed = 0;
        if (sscanf(end, " %7s%n", name, &consumed) != 1) continue;
        int category = -1;
        for (int c = 0; c < NUMCATEGORIES; ++c)
            if (!strcmp(name, categorynames[c])) category = c;
        if (category < 0) continue;
        sfxinfo_t group = { .priority = 200 };
        const char *rest = read_group(end + consumed, &group);
        if (category == CAT_ACK || category == CAT_SEL) {
            /* The trailing number orders barks; lower plays (0x42ef5c). */
            char *after;
            long priority = strtol(rest, &after, 10);
            if (after != rest) group.priority = (int)priority;
            group.norepeat = true;
        }
        if (!group.numlinks) continue;
        /* A repeated row replaces the earlier one, as the native load does. */
        sfxinfo_t *existing = S_Sfx(slist[id][category]);
        if (existing) *existing = group;
        else slist[id][category] = S_AddSfx(&group);
    }
    W_FreeFile(&file);
    return true;
}

static bool dc_sound_init(const char *root) {
    memset(slist, 0, sizeof(slist));
    if (!load_sound2(root) || !load_slist(root)) {
        fprintf(stderr, "warning: Dark Colony sound tables not found under %s/SOUND\n", root);
        return false;
    }
    for (int type = 0; type < DC_NUMBLOCKTYPES; ++type)
        for (int tod = 0; tod < 2; ++tod) ambience[type][tod] = S_AddSfx(&(sfxinfo_t){0});
    return true;
}

static void dc_sound_level_start(const level_t *map, const char *root) {
    for (int type = 0; type < DC_NUMBLOCKTYPES; ++type)
        for (int tod = 0; tod < 2; ++tod) {
            sfxinfo_t *group = S_Sfx(ambience[type][tod]);
            if (group) *group = (sfxinfo_t){0};
        }
    ambient_a = ambient_b = alert_unit = alert_base = 0;
    if (!map) return;

    /* 0x44e9e8: every tile id takes the block type most of its cells have. */
    static uint16_t votes[DC_NUMTILES][32];
    memset(votes, 0, sizeof(votes));
    memset(blocktypes, 0, sizeof(blocktypes));
    int count = map->width * map->height;
    for (int i = 0; map->tile_ids && map->tile_flags && i < count; ++i) {
        unsigned type = (unsigned)map->tile_flags[i] >> 10;
        if (type >= 32) continue;
        if (map->tile_ids[i] < DC_NUMTILES) votes[map->tile_ids[i]][type]++;
        uint16_t overlay = map->tile_overlays[0] ? map->tile_overlays[0][i] : 0;
        if (overlay && overlay < DC_NUMTILES) votes[overlay][type]++;
    }
    for (int tile = 0; tile < DC_NUMTILES; ++tile) {
        int best = 0;
        for (int type = 0; type < 32; ++type)
            if (votes[tile][type] > votes[tile][best]) best = type;
        blocktypes[tile] = (uint8_t)best;
    }

    blob_t file;
    char name[64];
    snprintf(name, sizeof(name), "SOUND/%s.AMB", map->tileset_name);
    if (!map->tileset_name[0] || !read_text(root, name, &file)) return;
    char *cursor = (char *)file.bytes, *line;
    while ((line = next_line(&cursor)) != NULL) {
        if (line[0] == '%' || strlen(line) < 3) continue;
        char *end, *end2;
        long tod = strtol(line, &end, 10);
        long type = strtol(end, &end2, 10);
        if (end == line || end2 == end || tod < 0 || tod > 1 ||
            type < 0 || type >= DC_NUMBLOCKTYPES) continue;
        sfxinfo_t *group = S_Sfx(ambience[type][tod]);
        if (group) read_group(end2, group);
    }
    W_FreeFile(&file);
}

static int weapon_sound(const mobj_t *actor) {
    if (actor->native_type_id >= GAMESTAT_UNIT_COUNT) return -1;
    const DcGamestatUnit *type = &dc_gamestat_units[actor->native_type_id];
    int weapon = type->values[GAMESTAT_UNIT_WEAPON0 + DC_WeaponLevel(actor)];
    if (weapon < 0) weapon = type->values[GAMESTAT_UNIT_WEAPON0];
    for (int i = 0; weapon >= 0 && i < GAMESTAT_WEAPON_COUNT; ++i)
        if (dc_gamestat_weapons[i].id == weapon) return dc_gamestat_weapons[i].sound;
    return -1;
}

static int category_sound(int id, category_t category) {
    return id >= 0 && id < DC_NUMSLISTIDS ? slist[id][category] : 0;
}

static int dc_actor_sound(const mobj_t *actor, soundevent_t event) {
    int native = actor->native_type_id;
    switch (event) {
    case SE_SELECT: return category_sound(native, CAT_SEL);
    case SE_ACK: return category_sound(native, CAT_ACK);
    case SE_DEATH: return category_sound(native, CAT_DEA);
    case SE_DEPLOY: return category_sound(native, CAT_DPY);
    case SE_ATTACK: return category_sound(weapon_sound(actor), CAT_GUN);
    case SE_EXPLODE:
        /* GAMESTAT explosion type 1 (BARR, PUS) and 2 (TURR) index EXP. */
        if (actor->type_id == MT_CANNONBALL || actor->type_id == MT_PUS_BOMB)
            return category_sound(1, CAT_EXP);
        if (actor->type_id == MT_TOWER_ROCKET) return category_sound(2, CAT_EXP);
        return 0;
    case SE_ATTACKED: {
        /* 0x4361ff/0x436262: the base and unit warnings repeat after 30 s. */
        bool base = actor->info && !(actor->info->traits & (MF_MOBILE | MF_ATTACK));
        uint32_t now = SDL_GetTicks(), *last = base ? &alert_base : &alert_unit;
        if (*last && now - *last < DC_ALERT_MS) return 0;
        *last = now ? now : 1;
        return DC_SFX(base ? SND_BASE : SND_UNIT);
    }
    case SE_ACTIVE:
        if (actor->type_id == MT_DROPSHIP)
            return DC_SFX(DC_PlayerRace(actor->owner) ? SND_DROPLPG : SND_DROPLP);
        return 0;
    default:
        return 0;
    }
}

/* 0x441c04: the commonest block type among visible cells in the view, with
 * block type 0 counted at a quarter. */
static int view_blocktype(const app_t *app, const level_t *map) {
    int view_w = G_WorldViewportWidth(app);
    cell_t a = R_ScreenToMapGrid(app, map, 0, 0);
    cell_t b = R_ScreenToMapGrid(app, map, view_w > 0 ? view_w - 1 : app->win.w - 1, app->win.h - 1);
    int x0 = a.x < b.x ? a.x : b.x, x1 = a.x < b.x ? b.x : a.x;
    int y0 = a.y < b.y ? a.y : b.y, y1 = a.y < b.y ? b.y : a.y;
    int counts[32] = {0}, total = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!L_Contains(map, x, y) || P_SightBrightness(map, (ivec2_t){x, y}) != 16) continue;
            int i = L_Index(map, x, y);
            uint16_t overlay = map->tile_overlays[0] ? map->tile_overlays[0][i] : 0;
            uint16_t tile = overlay ? overlay : map->tile_ids[i];
            counts[tile < DC_NUMTILES ? blocktypes[tile] : 0]++;
            total++;
        }
    if (!total) return -1;
    counts[0] /= 4;
    int best = 0;
    for (int type = 1; type < 32; ++type)
        if (counts[type] > counts[best]) best = type;
    return best;
}

static void dc_sound_ticker(const app_t *app, const level_t *map) {
    uint32_t now = app->ticks_ms;
    if (!ambient_a) ambient_a = ambient_b = now;
    if (now - ambient_a > DC_AMBIENT_A_MS) ambient_a = now;
    else if (now - ambient_b > DC_AMBIENT_B_MS) ambient_b = now;
    else return;
    int type = view_blocktype(app, map);
    if (type < 0 || type >= DC_NUMBLOCKTYPES) return;
    int tod = map->daylight.weight >= 128;
    const sfxinfo_t *group = S_Sfx(ambience[type][tod]);
    if (group && group->numlinks) S_StartLocalSound(ambience[type][tod]);
}

const soundinfo_t dc_soundinfo = {
    .init = dc_sound_init,
    .level_start = dc_sound_level_start,
    .actor_sound = dc_actor_sound,
    .ticker = dc_sound_ticker,
    .ui = {
        [UI_SOUND_CLICK] = DC_SFX(SND_BUTTON),
        [UI_SOUND_MESSAGE] = DC_SFX(SND_MSG),
        [UI_SOUND_SCREEN] = DC_SFX(SND_HUM),
        [UI_SOUND_GADGET] = DC_SFX(SND_ACTIVE),
    },
    .rolloff = 0.5f,
    .cutoff = 8000,
};
