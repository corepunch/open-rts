/* StarCraft sounds from the retail tables over the engine sound system.
 * sfxdata.dat names every WAV (column-major: tbl string u32, priority u8,
 * flags u8, portrait length u16, minimum volume u8; PyMS SoundsDAT); engine
 * sfx id n is sfxdata row n, and random groups follow the rows. units.dat
 * gives each unit its ready, what, pissed and yes ranges (PyMS UnitsDAT);
 * iscript.bin carries the weapon and death sounds: the first playsnd,
 * playsndrand, playsndbtwn or attackmelee of a unit's attack and death
 * animations, or of its weapon's bullet (weapons.dat graphics, a flingy).
 * Stargus's scripts/<race>/sound.lua agree on which ranges are which.
 * Nothing here touches game state or the gameplay RNG. */
#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    SC_MAX_SFX_ROWS = 1144, /* Brood War's row count; the original disc has 968. */
    /* sfxdata rows. Advisor and completion rows are Zerg, Terran, Protoss
     * triples; Stargus names upd00 "base attacked", upd01 "units attacked",
     * upd02 "research complete" and upd06 "upgrade complete". */
    SFX_BUTTON = 15,             /* Misc\Button.wav, also buildings' what sound. */
    SFX_BUZZ = 2, SFX_PLACEMENT = 4, /* Buzz, then Zerg/Terran/Protoss placement. */
    SFX_BASE_ATTACKED = 117,     /* ZAdUpd00, TAdUpd00, PAdUpd00 */
    SFX_UNITS_ATTACKED = 120,    /* ZAdUpd01, TAdUpd01, PAdUpd01 */
    SFX_RESEARCH_COMPLETE = 123, /* ZAdUpd02, TAdUpd02, PAdUpd02 */
    SFX_UPGRADE_COMPLETE = 131,  /* ZAdUpd02, TAdUpd06, PAdUpd06 */
    SFX_ZERG_MORPH_DONE = 135,   /* ZChRdy00 */
    SFX_TERRAN_BUILD_DONE = 136, /* TSCUpd00, the SCV's "job's finished" */
    SFX_UNIT_SPEECH = 0x02,      /* PyMS Sound.Flag.unit_speech */
    SFX_ONE_AT_A_TIME = 0x10,    /* PyMS Sound.Flag.one_at_a_time */
    /* This engine's choices: the click on one unit alone from which it is
     * annoyed, the most time between such clicks, and the least time
     * between two "under attack" warnings of the same kind. */
    SC_PISSED_CLICKS = 4, SC_PISSED_GAP_MS = 1500, SC_ALERT_GAP_MS = 10000,
};
enum { RACE_ZERG, RACE_TERRAN, RACE_PROTOSS, RACE_NONE };

typedef struct {
    uint16_t ready, what, yes; /* sfx ids: a row or a group */
    uint16_t pissed[2];        /* first and last row, played in order */
    uint16_t attack[2], death; /* ground and air attack, death */
} sc_unitsfx_t;

static sc_unitsfx_t unit_sfx[SC_TYPES];
static uint16_t weapon_sfx[SC_WEAPONS];
static int rows;
static bool used[SC_MAX_SFX_ROWS];
static bool unit_speech[SC_MAX_SFX_ROWS];
static uint32_t pissed_at, alert_at[2];
static uint32_t pissed_unit;
static int pissed_clicks, pissed_next[SC_TYPES];
int sc_dialog_click;
static int briefing_sfx, briefing_channel;
static int music_sfx, music_channel, music_mode = -1;
static bool music_missing;
static char music_root[1024];

void sc_stop_brief_voice(void) {
    S_StopChannel(briefing_channel);
    briefing_channel = 0;
    S_UnloadSound(briefing_sfx);
}

bool sc_brief_voice_playing(void) { return S_IsPlaying(briefing_channel); }

bool sc_unit_speech_muted(void) {
    const sc_mission_t *m = level.mission;
    return m && m->unit_speech_muted;
}

void sc_brief_voice(const char *scenario, const char *wav) {
    sc_stop_brief_voice();
    if (!S_Sfx(briefing_sfx) || !scenario || !wav || !*wav || *wav == '/' || *wav == '\\') return;
    char path[2048];
    const char *end = strrchr(scenario, '/');
    if (!end) return;
    const char *parent = end;
    while (parent > scenario && parent[-1] != '/') --parent;
    if (parent == scenario) return;
    size_t base = (size_t)(parent - scenario);
    if (base + strlen(wav) >= sizeof(path)) return;
    memcpy(path, scenario, base);
    size_t n = base;
    for (const char *p = wav; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == ':' || (c == '.' && p[1] == '.' && (p == wav || p[-1] == '/' || p[-1] == '\\') &&
                         (!p[2] || p[2] == '/' || p[2] == '\\'))) return;
        path[n++] = c == '\\' ? '/' : (char)tolower(c);
    }
    path[n] = 0;
    blob_t b = {0};
    if (!access(path, R_OK) && W_ReadFile(path, &b)) {
        if (S_LoadSound(briefing_sfx, b.bytes, b.size)) briefing_channel = S_StartLocalSound(briefing_sfx);
        W_FreeFile(&b);
    }
}

static int race_of(const sc_unit_t *u) {
    return u->race & 1 ? RACE_ZERG : u->race & 2 ? RACE_TERRAN : u->race & 4 ? RACE_PROTOSS : RACE_NONE;
}

int sc_placement_sound(bool success, int owner) {
    int race = sc_player_side(owner);
    return success && race >= 0 && race < 3 ? SFX_PLACEMENT + race : SFX_BUZZ;
}

/* One row, or a group of rows shared by every unit that names the same set. */
static int group(const uint16_t *ids, int count) {
    int16_t links[MAXSFXLINKS];
    int n = 0;
    for (int i = 0; i < count && n < MAXSFXLINKS; ++i)
        if (ids[i] > 0 && ids[i] < rows) links[n++] = (int16_t)ids[i];
    for (int i = 0; i < n; ++i) used[links[i]] = true;
    if (n <= 1) return n ? links[0] : 0;
    for (int id = rows; S_Sfx(id); ++id) {
        const sfxinfo_t *sfx = S_Sfx(id);
        if (sfx->numlinks == n && !memcmp(sfx->links, links, n * sizeof(*links))) return id;
    }
    sfxinfo_t sfx = {.numlinks = n, .norepeat = true};
    memcpy(sfx.links, links, n * sizeof(*links));
    return S_AddSfx(&sfx);
}

static int range(unsigned first, unsigned last) {
    uint16_t ids[MAXSFXLINKS];
    int n = 0;
    if (!first || first > last) return 0;
    for (unsigned id = first; id <= last && n < MAXSFXLINKS; ++id) ids[n++] = (uint16_t)id;
    return group(ids, n);
}

/* A ranged unit voice can be represented by a synthetic sound group. Filter
 * out flagged members so a mixed group still retains its non-speech samples
 * while mute is active. */
static int unmuted_variant(int id) {
    if (id <= 0) return 0;
    if (id < rows) return unit_speech[id] ? 0 : id;
    const sfxinfo_t *sfx = S_Sfx(id);
    if (!sfx || !sfx->numlinks) return 0;
    uint16_t ids[MAXSFXLINKS];
    int count = 0;
    for (int i = 0; i < sfx->numlinks; ++i) {
        int member = sfx->links[i];
        if (member > 0 && member < rows && !unit_speech[member]) ids[count++] = (uint16_t)member;
    }
    return group(ids, count);
}

typedef struct { blob_t units, flingy, sprites, images, weapons, script, dat, tbl; } sc_tables_t;

/* The iscript id of the image a flingy.dat row draws, or -1. */
static long flingy_script(const sc_tables_t *t, unsigned flingy) {
    unsigned ni = (unsigned)(t->images.size / 38);
    if (flingy >= t->flingy.size / 15) return -1;
    unsigned sprite = read_u16_le(t->flingy.bytes + flingy * 2);
    if (sprite * 2u + 2 > t->sprites.size) return -1;
    unsigned image = read_u16_le(t->sprites.bytes + sprite * 2);
    return image < ni ? (long)read_u32_le(t->images.bytes + ni * 10 + image * 4) : -1;
}

static int script_sound(const sc_tables_t *t, long script, int anim) {
    uint16_t ids[MAXSFXLINKS];
    return script < 0 ? 0 : group(ids, sc_iscript_sounds(&t->script, (unsigned)script, anim, ids, MAXSFXLINKS));
}

/* PyMS UnitsDAT column offsets; Brood War's file adds max ground and air
 * hits (18, 20) and its own flag (52). Columns 3 and 37 have 96 rows, ready
 * and the pissed and yes ranges (29, 32..35) have 106: units only. */
static void unit_columns(size_t size, int offsets[36]) {
    int bw = size == 19876;
    const int widths[36] = {1,2,2,2,4,1,1,2,4,1,1,1,1,1,1,1,1,1,bw,1,bw,1,4,1,1,1,1,1,1,2,2,2,2,2,2,2};
    for (int f = 0, at = 0; f < 36; ++f) {
        offsets[f] = at;
        at += widths[f] * (f == 3 ? 96 : f == 29 || (f >= 32 && f <= 35) ? 106 : 228);
    }
}

static void map_units(const sc_tables_t *t) {
    int col[36];
    unit_columns(t->units.size, col);
    const uint8_t *u = t->units.bytes;
    long scripts[SC_TYPES];
    for (int i = 0; i < SC_TYPES; ++i) scripts[i] = flingy_script(t, u[i]);
    for (int i = 0; i < SC_TYPES; ++i) {
        sc_unitsfx_t *s = &unit_sfx[i];
        s->what = (uint16_t)range(read_u16_le(u + col[30] + i * 2), read_u16_le(u + col[31] + i * 2));
        if (i < 106) {
            s->ready = (uint16_t)group((uint16_t[]){read_u16_le(u + col[29] + i * 2)}, 1);
            s->yes = (uint16_t)range(read_u16_le(u + col[34] + i * 2), read_u16_le(u + col[35] + i * 2));
            unsigned first = read_u16_le(u + col[32] + i * 2), last = read_u16_le(u + col[33] + i * 2);
            if (first && first <= last && last < (unsigned)rows) {
                s->pissed[0] = (uint16_t)first; s->pissed[1] = (uint16_t)last;
                for (unsigned id = first; id <= last; ++id) used[id] = true;
            }
        }
        s->death = (uint16_t)script_sound(t, scripts[i], 1);
        /* Goliaths and tanks fire from their turret's script; Carriers and
         * Reavers launch silently, and what they launch sounds for itself. */
        if (sc_hangar_type((uint16_t)(i + 1))) continue;
        int sub = sc_units[i].subunit;
        for (int air = 0; air < 2; ++air) {
            int sfx = script_sound(t, scripts[i], 2 + air);
            if (!sfx && sub >= 0 && sub < SC_TYPES) sfx = script_sound(t, scripts[sub], 2 + air);
            s->attack[air] = (uint16_t)sfx;
        }
    }
    /* A weapon whose unit's attack is silent sounds from its bullet's Init. */
    unsigned nw = (unsigned)(t->weapons.size / 42);
    for (unsigned w = 0; w < nw && w < SC_WEAPONS; ++w)
        weapon_sfx[w] = (uint16_t)script_sound(t, flingy_script(t, read_u32_le(t->weapons.bytes + 2 * nw + 4 * w)), 0);
    for (int race = 0; race < 3; ++race) {
        int ids[] = {SFX_BASE_ATTACKED, SFX_UNITS_ATTACKED, SFX_RESEARCH_COMPLETE, SFX_UPGRADE_COMPLETE};
        for (unsigned k = 0; k < sizeof(ids) / sizeof(*ids); ++k) used[ids[k] + race] = true;
    }
    used[SFX_BUTTON] = used[SFX_ZERG_MORPH_DONE] = used[SFX_TERRAN_BUILD_DONE] = true;
    used[SFX_BUZZ] = true;
    for (int i = 0; i < 3; ++i) used[SFX_PLACEMENT + i] = true;
}

static bool load(const char *root, int id, const char *name) {
    char path[256];
    blob_t wav = {0};
    snprintf(path, sizeof(path), "sound/%s", name);
    bool ok = sc_read(root, path, &wav) && S_LoadSound(id, wav.bytes, wav.size);
    W_FreeFile(&wav);
    if (ok) snprintf(S_Sfx(id)->name, sizeof(S_Sfx(id)->name), "%s", path);
    else fprintf(stderr, "starcraft: cannot load sound %s\n", path);
    return ok;
}

static bool sc_sound_init(const char *root) {
    sc_tables_t t = {0};
    bool ok = false;
    memset(unit_sfx, 0, sizeof(unit_sfx));
    memset(weapon_sfx, 0, sizeof(weapon_sfx));
    memset(used, 0, sizeof(used));
    memset(unit_speech, 0, sizeof(unit_speech));
    memset(pissed_next, 0, sizeof(pissed_next));
    pissed_clicks = 0; pissed_unit = 0; alert_at[0] = alert_at[1] = 0;
    sc_dialog_click = 0;
    briefing_sfx = briefing_channel = 0;
    music_sfx = music_channel = 0; music_mode = -1; music_missing = false;
    snprintf(music_root, sizeof(music_root), "%s", root);
    S_SetMusicVolume(gamesettings.music * 10);
    if (!sc_read(root, "arr/sfxdata.dat", &t.dat) || !sc_read(root, "arr/sfxdata.tbl", &t.tbl) ||
        !sc_read(root, "arr/units.dat", &t.units) || !sc_read(root, "arr/flingy.dat", &t.flingy) ||
        !sc_read(root, "arr/sprites.dat", &t.sprites) || !sc_read(root, "arr/images.dat", &t.images) ||
        !sc_read(root, "arr/weapons.dat", &t.weapons) || !sc_read(root, "scripts/iscript.bin", &t.script))
        goto done;
    rows = (int)(t.dat.size / 9);
    if (t.dat.size % 9 || rows <= SFX_TERRAN_BUILD_DONE || rows > SC_MAX_SFX_ROWS ||
        (t.units.size != 19192 && t.units.size != 19876) || t.weapons.size % 42 || t.images.size % 38)
        goto done;
    for (int id = 1; id < rows; ++id) {
        uint8_t flags = t.dat.bytes[rows * 5 + id];
        unit_speech[id] = (flags & SFX_UNIT_SPEECH) != 0;
        sfxinfo_t sfx = {.instances = flags & SFX_ONE_AT_A_TIME ? 1 : 0};
        if (S_AddSfx(&sfx) != id) goto done;
    }
    map_units(&t);
    for (int id = 1; id < rows; ++id) {
        const char *name = used[id] ? sc_tbl_string(&t.tbl, read_u32_le(t.dat.bytes + id * 4)) : NULL;
        if (name) load(root, id, name);
    }
    /* Dialog buttons press with the glue sound; it has no sfxdata row. */
    sc_dialog_click = S_AddSfx(&(sfxinfo_t){0});
    if (!sc_dialog_click || !load(root, sc_dialog_click, "glue\\mousedown2.wav")) sc_dialog_click = 0;
    briefing_sfx = S_AddSfx(&(sfxinfo_t){0});
    if (!briefing_sfx) goto done;
    music_sfx = S_AddSfx(&(sfxinfo_t){.music = true});
    if (!music_sfx) goto done;
    ok = true;
done:
    W_FreeFile(&t.dat); W_FreeFile(&t.tbl); W_FreeFile(&t.units); W_FreeFile(&t.flingy);
    W_FreeFile(&t.sprites); W_FreeFile(&t.images); W_FreeFile(&t.weapons); W_FreeFile(&t.script);
    if (!ok) fprintf(stderr, "starcraft: native sound tables are missing or malformed\n");
    return ok;
}

static void sc_music_tick(const app_t *app, const level_t *map) {
    (void)app;
    int mode = map ? sc_player_side(consoleplayer) : 3;
    if (mode < 0 || mode > 3) mode = 1;
    if (mode != music_mode) {
        S_StopChannel(music_channel); S_UnloadSound(music_sfx);
        music_channel = 0; music_mode = mode; music_missing = false;
    }
    if (music_missing || S_IsPlaying(music_channel)) return;
    if (!music_channel && S_Sfx(music_sfx)->data) {
        music_channel = S_StartLocalSound(music_sfx);
        return;
    }
    S_UnloadSound(music_sfx);
    static const char *const races[] = {"zerg", "terran", "protoss"};
    char name[64];
    if (mode == 3) snprintf(name, sizeof(name), "music/title.wav");
    else snprintf(name, sizeof(name), "music/%s%d.wav", races[mode], S_Random() % 3 + 1);
    char path[2048]; sc_asset_path(path, sizeof(path), music_root, name);
    blob_t wav = {0};
    if (access(path, R_OK) || !W_ReadFile(path, &wav)) { music_missing = true; return; }
    bool loaded = S_LoadSound(music_sfx, wav.bytes, wav.size);
    W_FreeFile(&wav);
    if (!loaded) { music_missing = true; return; }
    snprintf(S_Sfx(music_sfx)->name, sizeof(S_Sfx(music_sfx)->name), "%s", name);
    music_channel = S_StartLocalSound(music_sfx);
}

/* Whether actor is the only unit the local player has selected. */
static bool selected_alone(const mobj_t *actor) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && mo != actor && !mo->remove &&
            mo->owner == consoleplayer && P_MobjIsSelected(mo)) return false;
    }
    return true;
}

/* Clicking one unit again and again annoys it: its pissed rows play in
 * order from the SC_PISSED_CLICKS-th quick click on. */
static int select_sound(const mobj_t *actor, const sc_unitsfx_t *s) {
    uint32_t now = SDL_GetTicks();
    int type = actor->type_id - 1;
    bool again = actor->id == pissed_unit && now - pissed_at <= SC_PISSED_GAP_MS && selected_alone(actor);
    pissed_clicks = again ? pissed_clicks + 1 : 1;
    pissed_unit = actor->id;
    pissed_at = now;
    if (!s->pissed[0] || pissed_clicks < SC_PISSED_CLICKS) return s->what;
    int count = s->pissed[1] - s->pissed[0] + 1;
    return s->pissed[0] + pissed_next[type]++ % count;
}

static int sc_actor_sound(const mobj_t *actor, soundevent_t event) {
    const sc_unit_t *u = sc_unit(actor);
    if (!u) return 0;
    const sc_unitsfx_t *s = &unit_sfx[actor->type_id - 1];
    int race = race_of(u);
    bool building = (u->flags & SC_UNIT_BUILDING) != 0;
    int id = 0;
    switch (event) {
    case SE_SELECT: id = select_sound(actor, s); break;
    case SE_ACK: id = s->yes; break;
    case SE_READY:
        if (!building) id = s->ready;
        else id = race == RACE_TERRAN ? SFX_TERRAN_BUILD_DONE : race == RACE_ZERG ? SFX_ZERG_MORPH_DONE : 0;
        break;
    case SE_ATTACK: {
        const mobj_t *target = actor->attack.target;
        id = s->attack[target && (target->traits & MF_FLY) ? 1 : 0];
        if (!id && target && !sc_hangar_type(actor->type_id)) {
            const weapondef_t *w = P_MobjWeapon(actor, target);
            id = w && w->damage && w->native_id < SC_WEAPONS ? weapon_sfx[w->native_id] : 0;
        }
        break;
    }
    case SE_DEATH: id = s->death; break;
    case SE_RESEARCH_COMPLETE: {
        if (race == RACE_NONE) break;
        const production_t *p = actor->production;
        bool tech = p && p->product_type >= SC_TECH_UI && p->product_type < SC_TECH_UI + SC_TECHS;
        id = (tech ? SFX_RESEARCH_COMPLETE : SFX_UPGRADE_COMPLETE) + race;
        break;
    }
    case SE_ATTACKED: {
        if (race == RACE_NONE) break;
        uint32_t now = SDL_GetTicks();
        if (alert_at[building] && now - alert_at[building] < SC_ALERT_GAP_MS) break;
        alert_at[building] = now ? now : 1;
        id = (building ? SFX_BASE_ATTACKED : SFX_UNITS_ATTACKED) + race;
        break;
    }
    default: break;
    }
    return sc_unit_speech_muted() ? unmuted_variant(id) : id;
}

const soundinfo_t sc_soundinfo = {
    .init = sc_sound_init,
    .actor_sound = sc_actor_sound,
    .ticker = sc_music_tick,
    .ui = {[UI_SOUND_CLICK] = SFX_BUTTON},
};
