/* Dark Colony native sound tables. Shared playback rules are exercised by
 * tests/shared/test_sound.c, independently of these retail assets. */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"

#define REQUIRE(cond, msg) RTS_CHECK(cond, "sounds", msg)
#define DC_SFX(object) ((object) + 1)

static mobj_t unit_of(int native, int owner) {
    return (mobj_t){ .native_type_id = (uint16_t)native, .owner = (uint8_t)owner, .hp = 100,
                     .traits = MF_SELECTED };
}

static bool group_is(int id, const int *objects, int count) {
    const sfxinfo_t *sfx = S_Sfx(id);
    if (!sfx || sfx->numlinks != count) return false;
    for (int i = 0; i < count; ++i)
        if (sfx->links[i] != DC_SFX(objects[i])) return false;
    return true;
}

static int test_tables(void) {
    const sfxinfo_t *button = S_Sfx(DC_SFX(97));
    REQUIRE(button && !strcmp(button->name, "SOUND/BUTTON.WAV") && button->data,
            "SOUND2.DAT object 97 is the loaded BUTTON.WAV");
    const sfxinfo_t *drop = S_Sfx(DC_SFX(45));
    REQUIRE(drop && drop->loop && drop->instances == 5, "DROPLP loops with five buffers");
    const sfxinfo_t *whale = S_Sfx(DC_SFX(19));
    REQUIRE(whale && whale->volume == -600, "WHALE plays 6 dB down");

    mobj_t trooper = unit_of(0, 0);
    int select = dc_soundinfo.actor_sound(&trooper, SE_SELECT);
    REQUIRE(group_is(select, (const int[]){81, 83}, 2) && S_Sfx(select)->priority == 4,
            "trooper SEL is TRP1SEL/TRP3SEL at priority 4");
    REQUIRE(group_is(dc_soundinfo.actor_sound(&trooper, SE_DEATH),
                     (const int[]){28, 90, 153, 154}, 4), "trooper DEA list");
    /* GAMESTAT unit 0 fires weapon 1, whose sound value is SLIST GUN 1. */
    REQUIRE(group_is(dc_soundinfo.actor_sound(&trooper, SE_ATTACK),
                     (const int[]){91, 92, 93, 92, 92, 92, 92}, 7), "trooper GUN list");
    mobj_t engineer = unit_of(43, 0);
    REQUIRE(group_is(dc_soundinfo.actor_sound(&engineer, SE_DEPLOY), (const int[]){100}, 1),
            "engineer DPY is ENGDPLY");
    mobj_t psychic = unit_of(12, 0);
    REQUIRE(S_Sfx(dc_soundinfo.actor_sound(&trooper, SE_ACK))->priority == 4 &&
            S_Sfx(dc_soundinfo.actor_sound(&psychic, SE_ACK))->priority == 3,
            "native ACK priorities come from SLIST");
    REQUIRE(dc_soundinfo.rolloff == 0.5f && dc_soundinfo.cutoff == 8000,
            "DC supplies its native distance attenuation and cutoff");
    return 0;
}

static int find_group(const int *objects, int count) {
    for (int id = 1; S_Sfx(id); ++id)
        if (group_is(id, objects, count)) return id;
    return 0;
}

static int test_ambience(void) {
    /* JUNGLE.AMB: "%day water sounds" 0 3 26 25 23 61 61 61 -1. */
    static const int day_water[] = {26, 25, 23, 61, 61, 61};
    level_t map = {0};
    snprintf(map.tileset_name, sizeof(map.tileset_name), "JUNGLE");
    S_Start(&map, "data/DCOLONY");
    REQUIRE(find_group(day_water, 6), "the tileset's ambience is loaded per level");
    snprintf(map.tileset_name, sizeof(map.tileset_name), "NOSUCHSET");
    S_Start(&map, "data/DCOLONY");
    REQUIRE(!find_group(day_water, 6), "the next level replaces the ambience");
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    G_InitGame();
    consoleplayer = 0;
    REQUIRE(S_Init("data/DCOLONY"), "sound starts on the dummy audio driver");
    RTS_RUN(test_tables());
    RTS_RUN(test_ambience());
    S_Shutdown();
    puts("PASS: Dark Colony native sound tables, priorities, attenuation and ambience");
    return 0;
}
