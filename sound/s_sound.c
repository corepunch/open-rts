/* Doom's s_sound.c for an RTS. Sounds are started by the simulation and the
 * interface; this module chooses a channel, follows moving origins, and
 * attenuates and pans each sound against a listener at the centre of the
 * world view. The game supplies the sfx table and the event mapping. */
#include "engine.h"
#include <math.h>

#define MAXSFX 1024
#define NUMSCHANNELS 32
#define MAXGROUPDEPTH 4

typedef struct {
    int handle;         /* i_sound channel; 0 is free. */
    int sfx;            /* The sample playing (a group member, not the group). */
    const mobj_t *origin;
    bool positional;
    fvec2_t position;   /* Last known origin position. */
    int volume;         /* The sfx's centibel attenuation. */
} schannel_t;

bool nosound;
int snd_volume = 100;

static sfxinfo_t sfxtable[MAXSFX];
static int numsfx;
static schannel_t schannels[NUMSCHANNELS];
static int barkhandle, uihandles[NUMUISOUNDS];
static const soundinfo_t *game;
static const app_t *listener;
static bool initialized;
static uint32_t randseed = 0x2545f491u;

int S_Random(void) {
    /* xorshift32: independent of the deterministic gameplay table. */
    randseed ^= randseed << 13;
    randseed ^= randseed >> 17;
    randseed ^= randseed << 5;
    return (int)(randseed & 0x7fffffff);
}

int S_AddSfx(const sfxinfo_t *sfx) {
    if (!sfx || numsfx + 1 >= MAXSFX) return 0;
    sfxtable[++numsfx] = *sfx;
    sfxtable[numsfx].data = NULL;
    return numsfx;
}

sfxinfo_t *S_Sfx(int id) {
    return id > 0 && id <= numsfx ? &sfxtable[id] : NULL;
}

bool S_Init(const char *data_root) {
    if (initialized) return true;
    game = gameinfo ? gameinfo->sound : NULL;
    if (nosound || !game || !game->init) return false;
    if (!I_InitSound()) return false;
    numsfx = 0;
    if (!game->init(data_root)) {
        I_ShutdownSound();
        return false;
    }
    for (int i = 1; i <= numsfx; ++i) {
        if (!sfxtable[i].name[0]) continue;
        char path[1024];
        M_PathJoin(path, sizeof(path), data_root, sfxtable[i].name);
        sfxtable[i].data = I_LoadSample(path);
    }
    srand((unsigned)SDL_GetTicks());
    randseed ^= (uint32_t)rand() | 1u;
    initialized = true;
    return true;
}

void S_Shutdown(void) {
    if (!initialized) return;
    S_StopAllSounds();
    for (int i = 1; i <= numsfx; ++i) {
        I_FreeSample(sfxtable[i].data);
        sfxtable[i].data = NULL;
    }
    numsfx = 0;
    I_ShutdownSound();
    listener = NULL;
    initialized = false;
}

static void release(schannel_t *channel) {
    if (channel->handle) I_StopSound(channel->handle);
    *channel = (schannel_t){0};
}

void S_Start(const level_t *map, const char *data_root) {
    if (!initialized) return;
    for (int i = 0; i < NUMSCHANNELS; ++i) release(&schannels[i]);
    if (barkhandle) I_StopSound(barkhandle);
    barkhandle = 0;
    if (game->level_start) game->level_start(map, data_root);
}

/* A group plays its cursor member, then moves the cursor at random as
 * DC.EXE 0x42ec44 does; barks re-roll until the member differs (0x42ef5c). */
static int resolve(int id) {
    for (int depth = 0; depth < MAXGROUPDEPTH; ++depth) {
        sfxinfo_t *sfx = S_Sfx(id);
        if (!sfx || !sfx->numlinks) return sfx ? id : 0;
        int member = sfx->next < sfx->numlinks ? sfx->next : 0;
        id = sfx->links[member];
        if (sfx->numlinks > 1) {
            int next;
            do next = S_Random() % sfx->numlinks;
            while (sfx->norepeat && next == member);
            sfx->next = next;
        }
    }
    return 0;
}

/* Listener-relative volumes from DC.EXE 0x42ec44: attenuation in centibels
 * grows with the squared distance in cells. Panning follows the horizontal
 * offset from the view's centre rather than DC's distance-sized pan. */
static bool spatialize(fvec2_t position, int base, int *left, int *right) {
    double gain = snd_volume / 100.0;
    double pan = 0.0;
    if (listener && game) {
        float sx, sy;
        R_MapToScreen(listener, &level, position.x, position.y, &sx, &sy);
        int view_w = G_WorldViewportWidth(listener);
        if (view_w <= 0) view_w = listener->win.w;
        float cell_w = listener->cell.w > 0 ? (float)listener->cell.w : CELL_W;
        float cell_h = listener->cell.h > 0 ? (float)listener->cell.h : CELL_H;
        double dx = (sx - view_w * 0.5f) / cell_w;
        double dy = (sy - listener->win.h * 0.5f) / cell_h;
        double rolloff = game->rolloff * (dx * dx + dy * dy);
        if (game->cutoff && rolloff > game->cutoff) return false;
        base -= (int)rolloff;
        pan = dx / (view_w / cell_w);
        if (pan < -1.0) pan = -1.0;
        if (pan > 1.0) pan = 1.0;
    }
    gain *= pow(10.0, base / 2000.0);
    *left = (int)(256.0 * gain * (pan > 0 ? 1.0 - 0.7 * pan : 1.0) + 0.5);
    *right = (int)(256.0 * gain * (pan < 0 ? 1.0 + 0.7 * pan : 1.0) + 0.5);
    return true;
}

static void reap(void) {
    for (int i = 0; i < NUMSCHANNELS; ++i)
        if (schannels[i].handle && !I_SoundIsPlaying(schannels[i].handle))
            schannels[i] = (schannel_t){0};
}

static int start(const mobj_t *origin, bool positional, fvec2_t position, int id) {
    if (!initialized) return 0;
    id = resolve(id);
    sfxinfo_t *sfx = S_Sfx(id);
    if (!sfx || !sfx->data) return 0;
    reap();
    /* DC.EXE 0x42e0f0: a sample has `instances` buffers; with all of them
     * playing, the new request is dropped. */
    int playing = 0;
    for (int i = 0; i < NUMSCHANNELS; ++i)
        if (schannels[i].handle && schannels[i].sfx == id) ++playing;
    if (sfx->instances && playing >= sfx->instances) return 0;
    int left = 256, right = 256;
    if (positional && !spatialize(position, sfx->volume, &left, &right)) return 0;
    if (!positional) {
        double gain = snd_volume / 100.0 * pow(10.0, sfx->volume / 2000.0);
        left = right = (int)(256.0 * gain + 0.5);
    }
    int slot = -1;
    for (int i = 0; i < NUMSCHANNELS && slot < 0; ++i)
        if (!schannels[i].handle) slot = i;
    if (slot < 0) return 0;
    int handle = I_StartSound(sfx->data, left, right, sfx->loop);
    if (!handle) return 0;
    schannels[slot] = (schannel_t){
        .handle = handle, .sfx = id, .origin = origin, .positional = positional,
        .position = position, .volume = sfx->volume,
    };
    return handle;
}

int S_StartSound(const mobj_t *origin, int sfx) {
    if (!origin) return start(NULL, false, (fvec2_t){0}, sfx);
    return start(origin, true, fixed3_xy_to_fvec2(origin->core.position), sfx);
}

int S_StartSoundAt(fvec2_t position, int sfx) {
    return start(NULL, true, position, sfx);
}

int S_StartLocalSound(int sfx) {
    return start(NULL, false, (fvec2_t){0}, sfx);
}

void S_StartUISound(uisound_t sound) {
    if (!initialized || (unsigned)sound >= NUMUISOUNDS || !game->ui[sound]) return;
    if (uihandles[sound] && I_SoundIsPlaying(uihandles[sound])) {
        const sfxinfo_t *sfx = S_Sfx(game->ui[sound]);
        if (sfx && sfx->loop) return; /* A screen loop is already running. */
    }
    uihandles[sound] = S_StartLocalSound(game->ui[sound]);
}

void S_StopUISound(uisound_t sound) {
    if (!initialized || (unsigned)sound >= NUMUISOUNDS) return;
    S_StopChannel(uihandles[sound]);
    uihandles[sound] = 0;
}

void S_StopChannel(int handle) {
    if (!initialized || !handle) return;
    for (int i = 0; i < NUMSCHANNELS; ++i)
        if (schannels[i].handle == handle) release(&schannels[i]);
}

bool S_IsPlaying(int handle) {
    return initialized && handle && I_SoundIsPlaying(handle);
}

void S_StopSound(const mobj_t *origin) {
    if (!initialized || !origin) return;
    for (int i = 0; i < NUMSCHANNELS; ++i)
        if (schannels[i].origin == origin) release(&schannels[i]);
}

void S_UnlinkMobj(const mobj_t *origin) {
    if (!initialized || !origin) return;
    for (int i = 0; i < NUMSCHANNELS; ++i) {
        schannel_t *channel = &schannels[i];
        if (channel->origin != origin) continue;
        const sfxinfo_t *sfx = S_Sfx(channel->sfx);
        if (sfx && sfx->loop) release(channel);
        else channel->origin = NULL;
    }
}

void S_StopAllSounds(void) {
    if (!initialized) return;
    for (int i = 0; i < NUMSCHANNELS; ++i) release(&schannels[i]);
    if (barkhandle) I_StopSound(barkhandle);
    barkhandle = 0;
    for (int i = 0; i < NUMUISOUNDS; ++i) uihandles[i] = 0;
}

bool S_PositionVisible(const level_t *map, fvec2_t position) {
    ivec2_t cell = { (int)floorf(position.x), (int)floorf(position.y) };
    return map && P_SightBrightness(map, cell) == 16;
}

int S_ActorSound(const mobj_t *actor, soundevent_t event) {
    if (!initialized || !actor || !game->actor_sound) return 0;
    int sfx = game->actor_sound(actor, event);
    if (!sfx) return 0;
    /* Warnings speak to the player rather than come from the battle. */
    if (event == SE_ATTACKED) return S_StartLocalSound(sfx);
    /* DC.EXE 0x42edb8: world sounds play only from cells the local player
     * currently sees, so the fog cannot be heard through. */
    if (actor->owner != consoleplayer &&
        !S_PositionVisible(&level, fixed3_xy_to_fvec2(actor->core.position))) return 0;
    return S_StartSound(actor, sfx);
}

void S_Bark(mobj_t *const *units, int count, soundevent_t event, bool selected_only) {
    if (!initialized || !units || !game->actor_sound) return;
    int best = 0, best_priority = INT32_MAX;
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        /* Only the owning player hears a unit answer. */
        if (!unit || unit->remove || unit->hp <= 0 || unit->owner != consoleplayer) continue;
        if (selected_only && !P_MobjIsSelected(unit)) continue;
        int sfx = game->actor_sound(unit, event);
        const sfxinfo_t *info = S_Sfx(sfx);
        /* DC.EXE 0x42ef5c keeps the lowest priority number per category. */
        if (info && info->priority < best_priority) {
            best = sfx;
            best_priority = info->priority;
        }
    }
    if (!best) return;
    /* One voice at a time: a new answer replaces the one still talking. */
    S_StopChannel(barkhandle);
    barkhandle = S_StartLocalSound(best);
}

void S_UpdateSounds(const app_t *app, const level_t *map) {
    if (!initialized) return;
    listener = app;
    reap();
    for (int i = 0; i < NUMSCHANNELS; ++i) {
        schannel_t *channel = &schannels[i];
        if (!channel->handle || !channel->positional) continue;
        if (channel->origin) {
            channel->position = fixed3_xy_to_fvec2(channel->origin->core.position);
            if (channel->origin->remove) {
                S_UnlinkMobj(channel->origin);
                if (!channel->handle) continue;
            }
        }
        int left, right;
        if (!spatialize(channel->position, channel->volume, &left, &right)) left = right = 0;
        I_UpdateSoundParams(channel->handle, left, right);
    }
    if (game->ticker && map) game->ticker(app, map);
}
