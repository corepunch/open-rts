/* Dark Colony sound tables and the engine's sound rules, on SDL's dummy
 * audio driver: SOUND2.DAT objects, SLIST.DAT category groups and bark
 * priorities, weapon sounds through GAMESTAT, tileset ambience, distance
 * cutoff, and barks only for the owning player. */
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
    return 0;
}

static int test_barks(void) {
    mobj_t trooper = unit_of(0, 0), psychic = unit_of(12, 0);
    sfxinfo_t *trooper_ack = S_Sfx(dc_soundinfo.actor_sound(&trooper, SE_ACK));
    sfxinfo_t *psychic_ack = S_Sfx(dc_soundinfo.actor_sound(&psychic, SE_ACK));
    REQUIRE(trooper_ack && psychic_ack && trooper_ack->priority == 4 && psychic_ack->priority == 3,
            "ACK priorities come from SLIST");
    mobj_t *units[2] = {&trooper, &psychic};

    /* The lower priority number answers; its group cursor moves on. */
    trooper_ack->next = psychic_ack->next = 0;
    S_Bark(units, 2, SE_ACK, true);
    REQUIRE(psychic_ack->next != 0 && trooper_ack->next == 0, "the psychic answers before the trooper");

    /* Another player's unit never answers the local player. */
    psychic.owner = 1;
    trooper_ack->next = psychic_ack->next = 0;
    S_Bark(units, 2, SE_ACK, true);
    REQUIRE(psychic_ack->next == 0 && trooper_ack->next != 0, "only the owner's unit answers");

    /* Unselected units stay quiet when only the selection answers. */
    trooper.traits = 0;
    trooper_ack->next = 0;
    S_Bark(units, 2, SE_ACK, true);
    REQUIRE(trooper_ack->next == 0, "an unselected unit stays quiet");
    return 0;
}

static int test_distance(void) {
    level_t saved = level;
    level.width = level.height = 256;
    app_t app = { .win = {640, 480}, .cell = {32, 32} };
    S_UpdateSounds(&app, NULL);
    int near = S_StartSoundAt((fvec2_t){10.0f, 248.0f}, DC_SFX(97));
    int far = S_StartSoundAt((fvec2_t){250.0f, 10.0f}, DC_SFX(80));
    REQUIRE(near, "a sound in view plays");
    REQUIRE(!far, "a sound past DC's -80 dB cutoff is dropped");
    S_StopAllSounds();
    level = saved;
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
    RTS_RUN(test_barks());
    RTS_RUN(test_distance());
    RTS_RUN(test_ambience());
    S_Shutdown();
    puts("PASS: Dark Colony sound tables, bark priority and ownership, distance cutoff, ambience");
    return 0;
}
