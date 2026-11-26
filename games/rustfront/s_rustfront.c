#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <stdio.h>
#include <string.h>

/* Rustfront's sound: every sample is synthesised by art/sfx.py into assets/sfx/ and
 * assets/music/ (docs/AUDIO.md). This file registers them with the engine's mixer
 * (sound/s_sound.c) and maps the engine's events to them:
 *
 *   weapon fire, impacts, deaths   positional, from the shooter, the shot or the victim
 *   select, move, attack           a voice per faction, heard by the unit's owner only
 *   unit or structure ready        announcements; under attack at most every 20 seconds
 *   construction                   a site's placing, its scaffolding and its completion
 *   ambience                       wind everywhere, the hum of the player's headquarters
 *   music                          a menu theme, and in game the player's faction's track */

typedef struct {
    const char *files[3];  /* Under assets/: variants are played at random, never twice running. */
    int volume;            /* Centibels, <= 0. */
    int instances;         /* At most this many at once; 0 is unlimited. */
    bool loop, music;
} rf_sounddef_t;

static const rf_sounddef_t SOUNDS[RF_SND_COUNT] = {
    [RF_SND_CLICK] = {.files = {"sfx/ui_click"}, .volume = -600, .instances = 2},
    [RF_SND_UNIT_READY] = {.files = {"sfx/ui_unit_ready"}, .volume = -200, .instances = 1},
    [RF_SND_STRUCTURE_READY] = {.files = {"sfx/ui_structure_ready"}, .volume = -200, .instances = 1},
    [RF_SND_LOW_POWER] = {.files = {"sfx/ui_low_power"}, .volume = -200, .instances = 1},
    [RF_SND_NO_FUNDS] = {.files = {"sfx/ui_no_funds"}, .volume = -300, .instances = 1},
    [RF_SND_UNDER_ATTACK] = {.files = {"sfx/ui_under_attack"}, .volume = -200, .instances = 1},

    [RF_SND_RIFLE] = {.files = {"sfx/w_rifle1", "sfx/w_rifle2"}, .volume = -500, .instances = 3},
    [RF_SND_MG] = {.files = {"sfx/w_mg"}, .volume = -550, .instances = 3},
    [RF_SND_CANNON] = {.files = {"sfx/w_cannon1", "sfx/w_cannon2"}, .volume = -350, .instances = 2},
    [RF_SND_ARTILLERY] = {.files = {"sfx/w_artillery"}, .volume = -250, .instances = 2},
    [RF_SND_ROCKET] = {.files = {"sfx/w_rocket"}, .volume = -450, .instances = 3},
    [RF_SND_FLAME] = {.files = {"sfx/w_flame"}, .volume = -450, .instances = 3},
    [RF_SND_BEAM] = {.files = {"sfx/w_beam1", "sfx/w_beam2"}, .volume = -500, .instances = 2},
    [RF_SND_BEAM_HEAVY] = {.files = {"sfx/w_beam_heavy"}, .volume = -300, .instances = 2},

    [RF_SND_HIT_BULLET] = {.files = {"sfx/hit_bullet1", "sfx/hit_bullet2", "sfx/hit_bullet3"}, .volume = -800, .instances = 1},
    [RF_SND_HIT_SHELL] = {.files = {"sfx/hit_shell"}, .volume = -400, .instances = 3},
    [RF_SND_BOOM_SMALL] = {.files = {"sfx/boom_small"}, .volume = -300, .instances = 3},
    [RF_SND_BOOM_LARGE] = {.files = {"sfx/boom_large"}, .volume = -150, .instances = 3},
    [RF_SND_COLLAPSE] = {.files = {"sfx/collapse"}, .volume = 0, .instances = 2},
    [RF_SND_ZAP] = {.files = {"sfx/zap"}, .volume = -600, .instances = 3},
    [RF_SND_ZAP_HEAVY] = {.files = {"sfx/zap_heavy"}, .volume = -300, .instances = 2},
    [RF_SND_FLAME_HIT] = {.files = {"sfx/flame_hit"}, .volume = -700, .instances = 2},
    [RF_SND_DIE_SOLDIER] = {.files = {"sfx/die_soldier1", "sfx/die_soldier2"}, .volume = -600, .instances = 2},

    [RF_SND_BUILD_PLACE] = {.files = {"sfx/build_place"}, .volume = -300, .instances = 1},
    [RF_SND_BUILD_SCAFFOLD] = {.files = {"sfx/build_scaffold1", "sfx/build_scaffold2"}, .volume = -700, .instances = 2},
    [RF_SND_BUILD_DONE] = {.files = {"sfx/build_done"}, .volume = -300, .instances = 1},

    [RF_SND_DIR_SELECT] = {.files = {"sfx/dir_select1", "sfx/dir_select2"}, .volume = -200},
    [RF_SND_DIR_MOVE] = {.files = {"sfx/dir_move1", "sfx/dir_move2"}, .volume = -200},
    [RF_SND_DIR_ATTACK] = {.files = {"sfx/dir_attack1", "sfx/dir_attack2"}, .volume = -200},
    [RF_SND_CL_SELECT] = {.files = {"sfx/cl_select1", "sfx/cl_select2"}, .volume = -250},
    [RF_SND_CL_MOVE] = {.files = {"sfx/cl_move1", "sfx/cl_move2"}, .volume = -250},
    [RF_SND_CL_ATTACK] = {.files = {"sfx/cl_attack1", "sfx/cl_attack2"}, .volume = -250},
    [RF_SND_LC_SELECT] = {.files = {"sfx/lc_select1", "sfx/lc_select2"}, .volume = -150},
    [RF_SND_LC_MOVE] = {.files = {"sfx/lc_move1", "sfx/lc_move2"}, .volume = -150},
    [RF_SND_LC_ATTACK] = {.files = {"sfx/lc_attack1", "sfx/lc_attack2"}, .volume = -150},

    [RF_SND_AMB_WIND] = {.files = {"sfx/amb_wind"}, .volume = -1100, .instances = 1, .loop = true},
    [RF_SND_AMB_BASE] = {.files = {"sfx/amb_base"}, .volume = -900, .instances = 1, .loop = true},

    [RF_SND_MUS_MENU] = {.files = {"music/mus_menu"}, .volume = -300, .instances = 1, .loop = true, .music = true},
    [RF_SND_MUS_DIRECTORATE] = {.files = {"music/mus_directorate"}, .volume = -300, .instances = 1, .loop = true, .music = true},
    [RF_SND_MUS_CLANS] = {.files = {"music/mus_clans"}, .volume = -300, .instances = 1, .loop = true, .music = true},
    [RF_SND_MUS_CONCORD] = {.files = {"music/mus_concord"}, .volume = -300, .instances = 1, .loop = true, .music = true},
};
/* soundinfo_t.ui names the click by its sfx id: it is registered first, and alone. */
_Static_assert(RF_SND_CLICK == 1, "RF_SND_CLICK must be the first sound");

typedef struct { uint16_t type; rf_sound_t sound; } rf_typesound_t;

/* What each shooter fires (g_combat.c launches the projectiles; machine guns hit at once). */
static const rf_typesound_t WEAPONS[] = {
    {MT_RIFLEMAN, RF_SND_RIFLE}, {MT_ROCKET, RF_SND_ROCKET}, {MT_SCOUT, RF_SND_MG},
    {MT_TANK, RF_SND_CANNON}, {MT_ARTILLERY, RF_SND_ARTILLERY}, {MT_APC, RF_SND_MG},
    {MT_GUN_TURRET, RF_SND_CANNON}, {MT_MASTODON, RF_SND_ARTILLERY},
    {MT_CL_TOWER, RF_SND_MG}, {MT_CL_TRIKE, RF_SND_MG}, {MT_CL_TRUCK, RF_SND_MG},
    {MT_CL_CRAWLER, RF_SND_CANNON}, {MT_CL_RIG, RF_SND_ROCKET}, {MT_CL_RAIDER, RF_SND_RIFLE},
    {MT_CL_TORCHER, RF_SND_FLAME},
    {MT_LC_TOWER, RF_SND_BEAM_HEAVY}, {MT_LC_SKIMMER, RF_SND_BEAM}, {MT_LC_GLAIVE, RF_SND_BEAM},
    {MT_LC_PRISM, RF_SND_BEAM_HEAVY}, {MT_LC_WARDEN, RF_SND_BEAM}, {MT_LC_LANCER, RF_SND_BEAM},
    {MT_PHANTOM, RF_SND_CANNON}, {MT_KESTREL, RF_SND_ROCKET}, {MT_SENTRY, RF_SND_MG},
    {MT_CL_SAPPER, RF_SND_RIFLE}, {MT_CL_CRUSHER, RF_SND_HIT_SHELL}, {MT_CL_BUZZARD, RF_SND_ARTILLERY},
    {MT_LC_STRIDER, RF_SND_BEAM_HEAVY}, {MT_LC_SERAPH, RF_SND_BEAM},
};

/* Where each projectile lands. */
static const rf_typesound_t IMPACTS[] = {
    {MT_SHELL, RF_SND_HIT_SHELL}, {MT_HEAVY_SHELL, RF_SND_BOOM_SMALL}, {MT_MISSILE, RF_SND_BOOM_SMALL},
    {MT_BOLT, RF_SND_ZAP}, {MT_HEAVY_BOLT, RF_SND_ZAP_HEAVY}, {MT_FLAME, RF_SND_FLAME_HIT},
    {MT_BOMB, RF_SND_BOOM_LARGE},
};

static const rf_sound_t VOICES[RF_FACTION_COUNT][3] = {
    [RF_DIRECTORATE] = {RF_SND_DIR_SELECT, RF_SND_DIR_MOVE, RF_SND_DIR_ATTACK},
    [RF_CLANS] = {RF_SND_CL_SELECT, RF_SND_CL_MOVE, RF_SND_CL_ATTACK},
    [RF_CONCORD] = {RF_SND_LC_SELECT, RF_SND_LC_MOVE, RF_SND_LC_ATTACK},
};

static const rf_sound_t TRACKS[RF_FACTION_COUNT] = {
    [RF_DIRECTORATE] = RF_SND_MUS_DIRECTORATE, [RF_CLANS] = RF_SND_MUS_CLANS,
    [RF_CONCORD] = RF_SND_MUS_CONCORD,
};

enum {
    ALERT_GAP_TICS = 20 * RTS_TICRATE,  /* between two warnings of attack or of low power */
    NAG_GAP_TICS = 2 * RTS_TICRATE,     /* between two of any other announcement */
    SCAFFOLD_GAP_MS = 1700,             /* between two bursts of work on a site */
    FACTION_CHECK_MS = 1000,            /* how often the music looks at the player's faction */
};

static int ids[RF_SND_COUNT];          /* the engine's sfx id: a sample, or a group of variants */
static int alert_tic[RF_SND_COUNT];    /* the leveltime each alert last played, + 1 */
static rf_sound_t music_track;
static int music_handle;
static int wind_handle, base_handle;
static uint32_t base_hq;
static uint32_t scaffold_at, faction_at;
static int player_faction;

int RF_SoundId(rf_sound_t sound) {
    return sound > RF_SND_NONE && sound < RF_SND_COUNT ? ids[sound] : 0;
}

int RF_SoundFiles(rf_sound_t sound, const char *out[3]) {
    int n = 0;
    if (sound <= RF_SND_NONE || sound >= RF_SND_COUNT) return 0;
    for (; n < 3 && SOUNDS[sound].files[n]; ++n) out[n] = SOUNDS[sound].files[n];
    return n;
}

static void reset_state(void) {
    memset(alert_tic, 0, sizeof(alert_tic));
    music_track = RF_SND_NONE;
    music_handle = wind_handle = base_handle = 0;
    base_hq = 0;
    scaffold_at = faction_at = 0;
    player_faction = -1;
}

/* soundinfo_t.init: every sample becomes an sfx (S_Init loads its WAV from the data root),
 * and a sound with variants a group over them. */
static bool rf_sound_init(const char *root) {
    (void)root;
    memset(ids, 0, sizeof(ids));
    reset_state();
    for (int s = RF_SND_NONE + 1; s < RF_SND_COUNT; ++s) {
        const rf_sounddef_t *def = &SOUNDS[s];
        int16_t members[3] = {0};
        int count = 0;
        for (; count < 3 && def->files[count]; ++count) {
            sfxinfo_t sfx = {.volume = def->volume, .instances = def->instances, .loop = def->loop,
                             .music = def->music};
            snprintf(sfx.name, sizeof(sfx.name), "assets/%s.wav", def->files[count]);
            members[count] = (int16_t)S_AddSfx(&sfx);
            if (!members[count]) return false;
        }
        if (count == 1) {
            ids[s] = members[0];
            continue;
        }
        sfxinfo_t group = {.numlinks = count, .norepeat = true};
        for (int i = 0; i < count; ++i) group.links[i] = members[i];
        ids[s] = S_AddSfx(&group);
        if (!ids[s]) return false;
    }
    return true;
}

static void rf_level_start(const level_t *map, const char *root) {
    (void)map; (void)root;
    reset_state();   /* S_Start has stopped every channel; the ticker starts what it needs. */
}

static bool is_structure(const mobj_t *unit) {
    return unit->info && unit->info->footprint.w > 0 && !(unit->traits & MF_MOBILE);
}

static rf_sound_t lookup(const rf_typesound_t *rows, size_t count, uint16_t type) {
    for (size_t i = 0; i < count; ++i)
        if (rows[i].type == type) return rows[i].sound;
    return RF_SND_NONE;
}

static rf_sound_t voice_for(const mobj_t *actor, int line) {
    int faction = RF_FactionOfType(actor->type_id);
    if (faction < 0) return RF_SND_NONE;
    /* A structure answers a click with a click; only units speak. */
    if (is_structure(actor)) return line == 0 ? RF_SND_CLICK : RF_SND_NONE;
    return VOICES[faction][line];
}

/* An alert is let through once per gap of game time: a long one for the warnings, which
 * a battle would otherwise repeat every tic, a short one for the rest. */
static bool alert_ready(rf_sound_t sound) {
    int gap = sound == RF_SND_UNDER_ATTACK || sound == RF_SND_LOW_POWER ? ALERT_GAP_TICS : NAG_GAP_TICS;
    if (alert_tic[sound] && leveltime - (alert_tic[sound] - 1) < gap) return false;
    alert_tic[sound] = leveltime + 1;
    return true;
}

static rf_sound_t event_sound(const mobj_t *actor, soundevent_t event) {
    uint16_t type = actor->type_id;
    switch (event) {
    case SE_SELECT: return voice_for(actor, 0);
    case SE_ACK: return voice_for(actor, 1);
    case SE_ACK_ATTACK: return voice_for(actor, 2);
    case SE_ATTACK: {
        rf_sound_t s = lookup(WEAPONS, sizeof(WEAPONS) / sizeof(*WEAPONS), type);
        return s ? s : (actor->traits & MF_ATTACK) && RF_FactionOfType(type) >= 0 ? RF_SND_RIFLE : RF_SND_NONE;
    }
    case SE_EXPLODE: return lookup(IMPACTS, sizeof(IMPACTS) / sizeof(*IMPACTS), type);
    case SE_DEATH:
        if (RF_FactionOfType(type) < 0) return RF_SND_NONE;
        if (RF_IsInfantry(type)) return RF_SND_DIE_SOLDIER;
        return is_structure(actor) ? RF_SND_COLLAPSE : RF_SND_BOOM_LARGE;
    case SE_ATTACKED:
        return alert_ready(RF_SND_UNDER_ATTACK) ? RF_SND_UNDER_ATTACK : RF_SND_NONE;
    case SE_NO_FUNDS:
        return alert_ready(RF_SND_NO_FUNDS) ? RF_SND_NO_FUNDS : RF_SND_NONE;
    case SE_READY:
        if (RF_FactionOfType(type) < 0) return RF_SND_NONE;
        return is_structure(actor) ? RF_SND_STRUCTURE_READY : RF_SND_UNIT_READY;
    default: return RF_SND_NONE;
    }
}

rf_sound_t RF_EventSound(const mobj_t *actor, soundevent_t event) {
    return actor ? event_sound(actor, event) : RF_SND_NONE;
}

static int rf_actor_sound(const mobj_t *actor, soundevent_t event) {
    return RF_SoundId(RF_EventSound(actor, event));
}

void RF_PlaySound(rf_sound_t sound, const mobj_t *origin) {
    int id = RF_SoundId(sound);
    if (!id) return;
    if (origin) S_StartSound(origin, id);
    else S_StartLocalSound(id);
}

bool RF_Alert(rf_sound_t sound) {
    if (!RF_SoundId(sound) || !alert_ready(sound)) return false;
    S_StartLocalSound(RF_SoundId(sound));
    return true;
}

/* ── the ticker: music, ambience, construction ───────────────────────────── */

rf_sound_t RF_MusicFor(const level_t *map, int faction) {
    if (!map) return RF_SND_MUS_MENU;
    return TRACKS[faction >= 0 && faction < RF_FACTION_COUNT ? faction : RF_DIRECTORATE];
}

rf_sound_t RF_MusicPlaying(void) {
    return S_IsPlaying(music_handle) ? music_track : RF_SND_NONE;
}

static void keep_music(rf_sound_t track) {
    if (track != music_track) {
        S_StopChannel(music_handle);
        music_handle = 0;
        music_track = track;
    }
    if (!S_IsPlaying(music_handle)) music_handle = S_StartLocalSound(RF_SoundId(track));
}

static const mobj_t *player_hq(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && unit->owner == consoleplayer && !unit->remove && unit->hp > 0 &&
            RF_IsRole(unit->type_id, RF_ROLE_HQ) && !RF_IsSite(unit)) return unit;
    }
    return NULL;
}

/* Every few seconds each construction site gives a burst of hammering where it stands. */
static void work_sites(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !unit->remove && unit->hp > 0 && RF_IsSite(unit))
            S_StartSound(unit, RF_SoundId(RF_SND_BUILD_SCAFFOLD));
    }
}

static void rf_ticker(const app_t *app, const level_t *map) {
    (void)app;
    uint32_t now = SDL_GetTicks();
    if (!map) {
        /* The front end: its theme, and no battlefield behind it. */
        S_StopChannel(wind_handle);
        S_StopChannel(base_handle);
        wind_handle = base_handle = 0;
        base_hq = 0;
        player_faction = -1;
        keep_music(RF_SND_MUS_MENU);
        return;
    }
    if (player_faction < 0 || now - faction_at >= FACTION_CHECK_MS) {
        int faction = RF_OwnerFaction(consoleplayer);
        if (faction >= 0) player_faction = faction;
        faction_at = now;
    }
    keep_music(RF_MusicFor(map, player_faction));
    if (!S_IsPlaying(wind_handle)) wind_handle = S_StartLocalSound(RF_SoundId(RF_SND_AMB_WIND));
    const mobj_t *hq = player_hq();
    if (!hq || hq->id != base_hq || !S_IsPlaying(base_handle)) {
        S_StopChannel(base_handle);
        base_handle = hq ? S_StartSound(hq, RF_SoundId(RF_SND_AMB_BASE)) : 0;
        base_hq = hq ? hq->id : 0;
    }
    if (now - scaffold_at >= SCAFFOLD_GAP_MS) {
        scaffold_at = now;
        work_sites();
    }
}

const soundinfo_t rf_soundinfo = {
    .init = rf_sound_init,
    .level_start = rf_level_start,
    .actor_sound = rf_actor_sound,
    .ticker = rf_ticker,
    .ui = {[UI_SOUND_CLICK] = 1},   /* the click is the first sfx registered: see RF_SND_CLICK */
    /* About -8 dB ten cells from the view's centre; silent past about 27. */
    .rolloff = 8.0f,
    .cutoff = 6000,
};
