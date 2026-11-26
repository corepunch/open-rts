#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront sound", #c)

/* Rustfront's sounds through the real mixer on SDL's dummy audio device: every registered
 * sound loads from assets/ and plays, the engine's events map to the right family, the
 * under-attack warning is rate-limited, and the music follows the player's faction. */

static mobj_t actor(uint16_t type, int owner) {
    const mobjtype_t *t = P_ActorType(type);
    return (mobj_t){.type_id = type, .owner = (uint8_t)owner, .hp = 100, .info = t,
                    .traits = t ? t->traits : 0};
}

static rf_sound_t sound_of(uint16_t type, soundevent_t event) {
    mobj_t a = actor(type, 0);
    return RF_EventSound(&a, event);
}

/* Every sound and every variant of it is decoded, and plays and stops on a mixer channel. */
static int test_every_sound_loads(void) {
    int samples = 0;
    for (int s = RF_SND_NONE + 1; s < RF_SND_COUNT; ++s) {
        int id = RF_SoundId(s);
        CHECK(id > 0 && S_Sfx(id));
        const char *files[3];
        int n = RF_SoundFiles(s, files);
        CHECK(n >= 1);
        const sfxinfo_t *sfx = S_Sfx(id);
        CHECK(n == 1 ? sfx->numlinks == 0 : sfx->numlinks == n);
        for (int i = 0; i < n; ++i) {
            const sfxinfo_t *member = n == 1 ? sfx : S_Sfx(sfx->links[i]);
            char want[64];
            snprintf(want, sizeof(want), "assets/%s.wav", files[i]);
            CHECK(member && !strcmp(member->name, want));
            if (!member->data) fprintf(stderr, "rustfront sound: %s did not load\n", want);
            CHECK(member->data);
            ++samples;
        }
        int handle = S_StartLocalSound(id);
        CHECK(handle && S_IsPlaying(handle));
        S_StopChannel(handle);
        CHECK(!S_IsPlaying(handle));
    }
    /* Nothing else is registered: every sfx belongs to one of the sounds above. */
    int registered = 0;
    for (int id = 1; S_Sfx(id); ++id) registered += S_Sfx(id)->name[0] != 0;
    CHECK(registered == samples);
    CHECK(RF_SoundId(RF_SND_CLICK) == 1 && gameinfo->sound->ui[UI_SOUND_CLICK] == 1);
    CHECK(S_Sfx(RF_SoundId(RF_SND_MUS_CLANS))->music && S_Sfx(RF_SoundId(RF_SND_MUS_CLANS))->loop);
    CHECK(S_Sfx(RF_SoundId(RF_SND_AMB_WIND))->loop && !S_Sfx(RF_SoundId(RF_SND_AMB_WIND))->music);
    printf("PASS: %d Rustfront sounds, %d samples, load and play\n", RF_SND_COUNT - 1, samples);
    return 0;
}

/* Every armed unit and tower of every faction has a weapon sound, every projectile an
 * impact, and every unit its own faction's voice for each order. */
static int test_events(void) {
    for (int f = 0; f < RF_FACTION_COUNT; ++f) {
        rf_sound_t select = RF_SND_NONE;
        for (uint16_t type = rf_factions[f].first; type <= rf_factions[f].last; ++type) {
            const mobjtype_t *t = P_ActorType(type);
            if (!t) continue;
            mobj_t a = actor(type, 0);
            if (t->traits & MF_ATTACK) CHECK(RF_EventSound(&a, SE_ATTACK) >= RF_SND_RIFLE &&
                                             RF_EventSound(&a, SE_ATTACK) <= RF_SND_BEAM_HEAVY);
            CHECK(RF_EventSound(&a, SE_DEATH) != RF_SND_NONE);
            CHECK(RF_EventSound(&a, SE_READY) != RF_SND_NONE);
            if (t->traits & MF_MOBILE) {
                rf_sound_t s = RF_EventSound(&a, SE_SELECT);
                CHECK(s != RF_SND_NONE && (select == RF_SND_NONE || s == select));
                select = s;
                CHECK(RF_EventSound(&a, SE_ACK) == s + 1 && RF_EventSound(&a, SE_ACK_ATTACK) == s + 2);
            }
        }
        CHECK(select == (rf_sound_t)(RF_SND_DIR_SELECT + 3 * f));
    }
    CHECK(sound_of(MT_RIFLEMAN, SE_ATTACK) == RF_SND_RIFLE && sound_of(MT_SCOUT, SE_ATTACK) == RF_SND_MG);
    CHECK(sound_of(MT_TANK, SE_ATTACK) == RF_SND_CANNON && sound_of(MT_ROCKET, SE_ATTACK) == RF_SND_ROCKET);
    CHECK(sound_of(MT_CL_TORCHER, SE_ATTACK) == RF_SND_FLAME && sound_of(MT_LC_GLAIVE, SE_ATTACK) == RF_SND_BEAM);
    CHECK(sound_of(MT_LC_PRISM, SE_ATTACK) == RF_SND_BEAM_HEAVY);
    CHECK(sound_of(MT_SHELL, SE_EXPLODE) == RF_SND_HIT_SHELL && sound_of(MT_MISSILE, SE_EXPLODE) == RF_SND_BOOM_SMALL);
    CHECK(sound_of(MT_BOLT, SE_EXPLODE) == RF_SND_ZAP && sound_of(MT_FLAME, SE_EXPLODE) == RF_SND_FLAME_HIT);
    CHECK(sound_of(MT_HEAVY_BOLT, SE_EXPLODE) == RF_SND_ZAP_HEAVY);
    CHECK(sound_of(MT_RIFLEMAN, SE_DEATH) == RF_SND_DIE_SOLDIER && sound_of(MT_TANK, SE_DEATH) == RF_SND_BOOM_LARGE);
    CHECK(sound_of(MT_HQ, SE_DEATH) == RF_SND_COLLAPSE && sound_of(MT_LC_SPIRE, SE_READY) == RF_SND_STRUCTURE_READY);
    CHECK(sound_of(MT_CL_TRIKE, SE_READY) == RF_SND_UNIT_READY);
    CHECK(sound_of(MT_HQ, SE_SELECT) == RF_SND_CLICK && sound_of(MT_HQ, SE_ACK) == RF_SND_NONE);
    CHECK(sound_of(MT_FX_BOOM, SE_DEATH) == RF_SND_NONE && sound_of(MT_SHELL, SE_ATTACK) == RF_SND_NONE);
    puts("PASS: weapons, impacts, deaths, readiness and each faction's voices");
    return 0;
}

/* A battle damages the player's units every tic; the warning sounds once per 20 seconds. */
static int test_alert_rate(void) {
    int saved = leveltime;
    leveltime = 1000;
    CHECK(sound_of(MT_TANK, SE_ATTACKED) == RF_SND_UNDER_ATTACK);
    for (int t = 1; t < 20 * RTS_TICRATE; t += 7) {
        leveltime = 1000 + t;
        CHECK(sound_of(MT_TANK, SE_ATTACKED) == RF_SND_NONE);
    }
    leveltime = 1000 + 20 * RTS_TICRATE;
    CHECK(sound_of(MT_HQ, SE_ATTACKED) == RF_SND_UNDER_ATTACK);
    /* Clicking what the player cannot afford says so sooner, but not on every click. */
    CHECK(sound_of(MT_HQ, SE_NO_FUNDS) == RF_SND_NO_FUNDS && !RF_Alert(RF_SND_NO_FUNDS));
    leveltime += 2 * RTS_TICRATE;
    CHECK(RF_Alert(RF_SND_NO_FUNDS) && sound_of(MT_LC_FOUNDRY, SE_NO_FUNDS) == RF_SND_NONE);
    leveltime = saved;
    puts("PASS: the under-attack warning is rate-limited");
    return 0;
}

/* The menu plays its theme; a game plays the player's faction's track, and the ticker keeps
 * it and the ambience going. */
static int test_music(void) {
    app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
    CHECK(RF_MusicFor(NULL, RF_CONCORD) == RF_SND_MUS_MENU);
    CHECK(RF_MusicFor(&level, RF_DIRECTORATE) == RF_SND_MUS_DIRECTORATE);
    CHECK(RF_MusicFor(&level, RF_CLANS) == RF_SND_MUS_CLANS && RF_MusicFor(&level, -1) == RF_SND_MUS_DIRECTORATE);
    S_UpdateSounds(&app, NULL);
    CHECK(RF_MusicPlaying() == RF_SND_MUS_MENU);
    static const struct { const char *map; rf_sound_t track; } MAPS[] = {
        {"maps/triad.txt", RF_SND_MUS_DIRECTORATE}, {"maps/triad-clans.txt", RF_SND_MUS_CLANS},
        {"maps/triad-concord.txt", RF_SND_MUS_CONCORD},
    };
    for (size_t i = 0; i < sizeof(MAPS) / sizeof(*MAPS); ++i) {
        RtsGameModel *model = rts_game_model_create();
        RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = MAPS[i].map};
        CHECK(rts_game_model_load(model, &config));
        S_Start(&level, g_game_default_root);
        S_UpdateSounds(&app, &level);
        CHECK(RF_MusicPlaying() == MAPS[i].track);
        S_UpdateSounds(&app, &level);
        CHECK(RF_MusicPlaying() == MAPS[i].track);
        rts_game_model_destroy(model);
    }
    puts("PASS: the menu theme, then each faction's own track");
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(gameinfo->sound == &rf_soundinfo);
    CHECK(S_Init(g_game_default_root));
    RTS_RUN(test_every_sound_loads());
    RTS_RUN(test_events());
    RTS_RUN(test_alert_rate());
    RTS_RUN(test_music());
    S_Shutdown();
    SDL_Quit();
    return 0;
}
