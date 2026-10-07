/* Native Warcraft II sound bank and actual mixer handles, without a device. */
#include "engine.h"
#include "info.h"
#include "warcraft-2.h"
#include "t_local.h"
#include <sys/stat.h>
#include <unistd.h>

#define REQUIRE(c, m) RTS_CHECK(c, "warcraft-2 sounds", m)

static mobj_t unit_of(int type, int owner) {
    return (mobj_t){.type_id = (uint16_t)type, .owner = (uint8_t)owner,
                    .hp = 100, .traits = MF_SELECTED};
}

static int sound_for(int type, soundevent_t event) {
    mobj_t actor = unit_of(type, 0);
    return gameinfo->sound->actor_sound(&actor, event);
}

static bool group_is(int id, const int *entries, int count) {
    const sfxinfo_t *sfx = S_Sfx(id);
    if (!sfx || sfx->numlinks != count) return false;
    for (int i = 0; i < count; ++i)
        if (sfx->links[i] != entries[i]) return false;
    return true;
}

static int test_samples(void) {
    REQUIRE(S_Sfx(1)->data && !strcmp(S_Sfx(1)->name, "DATA/MAINDAT.WAR#432"),
            "native UI click is loaded from MAINDAT");
    int loaded = 0, groups = 0;
    for (int id = 1; S_Sfx(id); ++id) {
        const sfxinfo_t *sfx = S_Sfx(id);
        if (sfx->numlinks) {
            groups++;
            for (int i = 0; i < sfx->numlinks; ++i)
                REQUIRE(S_Sfx(sfx->links[i]) && S_Sfx(sfx->links[i])->data,
                        "every group member has decoded audio");
        }
        if (!sfx->name[0]) continue;
        REQUIRE(sfx->data, "every registered sample is decoded");
        int handle = S_StartLocalSound(id);
        REQUIRE(handle && S_IsPlaying(handle), "native sample plays through the mixer");
        S_StopChannel(handle);
        REQUIRE(!S_IsPlaying(handle), "native sample stops");
        loaded++;
    }
    printf("Warcraft II: %d native WAV samples and %d sound groups verified\n", loaded, groups);
    REQUIRE(loaded == 185 && groups == 35, "load each referenced native sample once");
    return 0;
}

static int test_mappings(void) {
    REQUIRE(group_is(sound_for(MT_FOOTMAN, SE_SELECT), (int[]){5, 7, 9, 11, 13, 15}, 6),
            "human selections preserve the interleaved archive indices");
    REQUIRE(group_is(sound_for(MT_GRUNT, SE_ACK), (int[]){33, 35, 37, 39}, 4), "orc orders");
    REQUIRE(group_is(sound_for(MT_PEASANT, SE_SELECT), (int[]){271, 272, 273, 274}, 4),
            "peasants have their own voice");
    REQUIRE(sound_for(MT_PEON, SE_SELECT) == sound_for(MT_GRUNT, SE_SELECT), "peon selection alias");
    REQUIRE(sound_for(MT_PEON, SE_READY) == 115 && sound_for(MT_PEASANT, SE_READY) == 263,
            "worker ready announcements");
    REQUIRE(group_is(sound_for(MT_KNIGHT, SE_ATTACK), (int[]){60, 61, 62}, 3), "sword variants");
    REQUIRE(sound_for(MT_ARCHER, SE_ATTACK) == 66 && sound_for(MT_AXETHROWER, SE_ATTACK) == 77,
            "bow and throwing axe differ");
    REQUIRE(sound_for(MT_MAGE, SE_ATTACK) == 111 && sound_for(MT_DEATH_KNIGHT, SE_ATTACK) == 112,
            "caster weapons differ");
    REQUIRE(sound_for(MT_HUMAN_OIL_TANKER, SE_ACK) == 78 &&
            sound_for(MT_HUMAN_DESTROYER, SE_DEATH) == 51, "ship motor and sinking");
    REQUIRE(sound_for(MT_FARM, SE_SELECT) == 74 && sound_for(MT_PIG_FARM, SE_SELECT) == 75,
            "building selection effects");
    REQUIRE(sound_for(MT_DRAGON, SE_DEATH) == 31 && !sound_for(MT_EYE_OF_KILROGG, SE_DEATH),
            "dragon explodes, Eye has no reference death sound");
    REQUIRE(group_is(sound_for(MT_TOWN_HALL, SE_DEATH), (int[]){52, 53, 54}, 3),
            "building destruction variants");
    REQUIRE(sound_for(MT_PEASANT, SE_WORK_COMPLETE) == 42 &&
            sound_for(MT_PEON, SE_WORK_COMPLETE) == 41, "construction announcements");
    REQUIRE(!sound_for(MT_RESERVED_34, SE_SELECT) && !sound_for(NUMMOBJTYPES, SE_SELECT),
            "unknown types remain silent");
    REQUIRE(states[W2_WORK_STATE(2) + 3].action && states[W2_WORK_STATE(2) + 3].frame == 8,
            "chopping sounds fire at Wargus's frame 40 (logical frame 8)");
    return 0;
}

static int test_events(void) {
    mobj_t peasant = unit_of(MT_PEASANT, 0);
    mobj_t *selection[] = {&peasant};
    sfxinfo_t *ack = S_Sfx(sound_for(MT_PEASANT, SE_ACK));
    uint32_t rng = W2_CombatState();
    ack->next = 0;
    S_Bark(selection, 1, SE_ACK, true);
    REQUIRE(ack->next != 0, "selected local worker answers");
    peasant.owner = 1;
    ack->next = 0;
    S_Bark(selection, 1, SE_ACK, true);
    REQUIRE(ack->next == 0, "enemy orders stay silent");
    peasant.owner = 0;
    peasant.traits = 0;
    S_Bark(selection, 1, SE_ACK, true);
    REQUIRE(ack->next == 0, "unselected unit does not answer selected-only orders");
    S_Bark(selection, 1, SE_ACK, false);
    REQUIRE(ack->next != 0, "completion barks can use unselected units");
    int attack = S_ActorSound(&peasant, SE_ATTACK);
    REQUIRE(attack && S_IsPlaying(attack), "weapon event starts native audio");
    S_SetVolume(0);
    REQUIRE(S_IsPlaying(attack) && snd_volume == 0, "volume controls existing playback");
    S_SetVolume(100);
    S_UnlinkMobj(&peasant);
    REQUIRE(S_IsPlaying(attack), "one-shot sound survives origin removal");
    S_Start(NULL, "data/WAR2");
    REQUIRE(!S_IsPlaying(attack), "level start stops old sounds");
    REQUIRE(W2_CombatState() == rng, "audio never advances deterministic combat RNG");
    uint32_t hidden = 0;
    level.width = level.height = 1;
    level.sight.cells = &hidden;
    peasant.owner = 1;
    REQUIRE(!S_ActorSound(&peasant, SE_WORK), "enemy chopping cannot be heard through fog");
    level.sight.cells = NULL;
    level.width = level.height = 0;
    return 0;
}

static int test_failed_init(void) {
    /* The click loads before the second archive fails. Its sample must be
     * freed, and a later S_Init must begin again at sound id one. */
    char root[] = "/private/tmp/open-rts-w2-sound-XXXXXX";
    REQUIRE(mkdtemp(root), "temporary missing-bank fixture");
    char data[1024], link[1024], source[2048], cwd[1024];
    M_PathJoin(data, sizeof(data), root, "DATA");
    REQUIRE(mkdir(data, 0700) == 0 && getcwd(cwd, sizeof(cwd)), "fixture directory");
    M_PathJoin(source, sizeof(source), cwd, "data/WAR2/DATA/MAINDAT.WAR");
    M_PathJoin(link, sizeof(link), data, "MAINDAT.WAR");
    REQUIRE(symlink(source, link) == 0, "fixture native click archive");
    bool started = S_Init(root);
    unlink(link);
    rmdir(data);
    rmdir(root);
    REQUIRE(!started && !S_Sfx(1), "partial sound bank is discarded on failure");
    REQUIRE(S_Init("data/WAR2"), "initialization recovers after partial failure");
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    G_InitGame();
    consoleplayer = 0;
    REQUIRE(S_Init("data/WAR2"), "native sound initializes");
    RTS_RUN(test_samples());
    RTS_RUN(test_mappings());
    RTS_RUN(test_events());
    S_Shutdown();
    REQUIRE(!S_Sfx(1), "shutdown frees the table");
    RTS_RUN(test_failed_init());
    S_Shutdown();
    nosound = true;
    REQUIRE(!S_Init("data/WAR2") && !S_ActorSound(&(mobj_t){0}, SE_ATTACK), "nosound stays silent");
    nosound = false;
    puts("PASS: Warcraft II native sounds, events, ownership, volume and reload lifecycle");
    return 0;
}
