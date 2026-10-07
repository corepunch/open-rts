/* Engine sound ownership and playback, independent of retail assets or game
 * sound definitions. Every game binary runs the same synthetic WAV fixture. */
#include "engine.h"
#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "engine sound", #c)
enum { TONE = 1, OTHER, LOOP, EMPTY, VOICE, PRIORITY_VOICE };
static bool fail_init;

static void put32(uint8_t *p, unsigned value) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static bool init_sounds(const char *root) {
    (void)root;
    for (int id = TONE; id <= EMPTY; ++id)
        if (S_AddSfx(&(sfxinfo_t){.loop = id == LOOP}) != id) return false;
    if (S_AddSfx(&(sfxinfo_t){.links = {TONE, OTHER}, .numlinks = 2,
                            .priority = 4, .norepeat = true}) != VOICE ||
        S_AddSfx(&(sfxinfo_t){.links = {OTHER, TONE}, .numlinks = 2,
                            .priority = 3, .norepeat = true}) != PRIORITY_VOICE) return false;
    /* One second of unsigned 8-bit mono PCM at 8000 Hz. The engine must own
     * its converted copy after this stack buffer goes out of scope. */
    uint8_t wav[44 + 8000] = {0};
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, sizeof(wav) - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    wav[20] = wav[22] = wav[32] = 1;
    put32(wav + 24, 8000);
    put32(wav + 28, 8000);
    wav[34] = 8;
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, sizeof(wav) - 44);
    memset(wav + 44, 144, sizeof(wav) - 44);
    for (int id = TONE; id <= LOOP; ++id)
        if (!S_LoadSound(id, wav, sizeof(wav))) return false;
    return !fail_init;
}

static int actor_sound(const mobj_t *actor, soundevent_t event) {
    if (event == SE_ACK) return actor->type_id == 1 ? VOICE : PRIORITY_VOICE;
    return event == SE_ATTACK ? TONE : 0;
}

static const soundinfo_t sounds = {
    .init = init_sounds, .actor_sound = actor_sound,
    .ui = {[UI_SOUND_SCREEN] = LOOP}, .rolloff = 0.5f, .cutoff = 8000,
};

static int test_loading(void) {
    CHECK(S_Sfx(TONE)->data && S_Sfx(OTHER)->data && S_Sfx(LOOP)->data);
    CHECK(!S_LoadSound(0, "RIFF", 4));
    CHECK(!S_LoadSound(EMPTY, "RIFF", 4));
    CHECK(!S_LoadSound(EMPTY, NULL, 10));
    CHECK(!S_LoadSound(EMPTY, "x", (size_t)INT32_MAX + 1));
    CHECK(!S_Sfx(EMPTY)->data);
    /* A loaded sample or a group cannot be overwritten by a native loader. */
    CHECK(!S_LoadSound(TONE, "RIFF", 4) && S_Sfx(TONE)->data);
    CHECK(!S_LoadSound(VOICE, "RIFF", 4) && !S_Sfx(VOICE)->data);
    return 0;
}

static int test_channels(void) {
    mobj_t origin = {.hp = 1};
    int once = S_StartSound(&origin, TONE);
    int loop = S_StartSound(&origin, LOOP);
    CHECK(once && loop && S_IsPlaying(once) && S_IsPlaying(loop));
    S_SetVolume(0);
    CHECK(snd_volume == 0 && S_IsPlaying(once));
    S_SetVolume(100);
    S_UnlinkMobj(&origin);
    CHECK(S_IsPlaying(once) && !S_IsPlaying(loop));
    S_StopAllSounds();
    CHECK(!S_IsPlaying(once));
    int next = S_StartLocalSound(TONE);
    CHECK(next && S_IsPlaying(next) && !S_IsPlaying(once));
    S_StopChannel(once);
    CHECK(S_IsPlaying(next));
    S_Start(NULL, "");
    CHECK(!S_IsPlaying(next));
    S_StartUISound(UI_SOUND_SCREEN);
    S_StopUISound(UI_SOUND_SCREEN);
    return 0;
}

static int test_barks(void) {
    mobj_t first = {.type_id = 1, .hp = 100, .traits = MF_SELECTED};
    mobj_t preferred = {.type_id = 2, .hp = 100, .traits = MF_SELECTED};
    mobj_t *units[] = {&first, &preferred};
    sfxinfo_t *voice = S_Sfx(VOICE), *priority = S_Sfx(PRIORITY_VOICE);
    S_Bark(units, 2, SE_ACK, true);
    CHECK(priority->next == 1 && voice->next == 0);
    priority->next = 0;
    preferred.owner = 1;
    S_Bark(units, 2, SE_ACK, true);
    CHECK(priority->next == 0 && voice->next == 1);
    voice->next = 0;
    first.traits = 0;
    S_Bark(units, 2, SE_ACK, true);
    CHECK(voice->next == 0);
    S_Bark(units, 2, SE_ACK, false);
    CHECK(voice->next == 1);
    S_StopAllSounds();
    return 0;
}

static int test_spatial(void) {
    level.width = level.height = 256;
    app_t app = {.win = {640, 480}, .cell = {32, 32}};
    S_UpdateSounds(&app, NULL);
    cell_t centre = R_ScreenToMapGrid(&app, &level, 320, 240);
    int near = S_StartSoundAt((fvec2_t){centre.x, centre.y}, TONE);
    CHECK(near);
    CHECK(!S_StartSoundAt((fvec2_t){centre.x + 200, centre.y + 200}, OTHER));
    uint32_t hidden = 0;
    level.width = level.height = 1;
    level.sight.cells = &hidden;
    mobj_t enemy = {.hp = 100, .owner = 1};
    CHECK(!S_ActorSound(&enemy, SE_ATTACK));
    level.sight.cells = NULL;
    S_StopAllSounds();
    S_UpdateSounds(NULL, NULL);
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const gameinfo_t *saved = gameinfo;
    gameinfo_t fixture = {.sound = &sounds};
    gameinfo = &fixture;
    consoleplayer = 0;
    CHECK(S_Init(""));
    RTS_RUN(test_loading());
    RTS_RUN(test_channels());
    RTS_RUN(test_barks());
    RTS_RUN(test_spatial());
    S_Shutdown();
    CHECK(!S_Sfx(TONE));
    fail_init = true;
    CHECK(!S_Init("") && !S_Sfx(TONE));
    fail_init = false;
    CHECK(S_Init(""));
    int handle = S_StartLocalSound(TONE);
    CHECK(handle && S_IsPlaying(handle));
    S_Shutdown();
    CHECK(!S_IsPlaying(handle));
    nosound = true;
    CHECK(!S_Init("") && !S_StartLocalSound(TONE));
    nosound = false;
    gameinfo = saved;
    puts("PASS: engine sound loading, channels, barks, visibility and lifecycle without retail data");
    return 0;
}
