/* StarCraft's native sound bank on the dummy audio driver: units.dat
 * response ranges resolved through sfxdata.dat, iscript weapon and death
 * sounds, the annoyed sequence, advisor lines and the UI clicks. */
#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft sounds", #c)

static const char *root = "data/STARCRAFT";

static mobj_t *spawn(int type, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2((fixed2_t){FIXED_FROM_INT(4), FIXED_FROM_INT(4)}, 0), (uint16_t)type);
    if (u) u->owner = u->team = (uint8_t)owner;
    return u;
}
static int sound_for(const mobj_t *actor, soundevent_t event) {
    return gameinfo->sound->actor_sound(actor, event);
}
/* A group of consecutive sfxdata rows first..last. */
static bool rows(int id, int first, int last) {
    const sfxinfo_t *sfx = S_Sfx(id);
    if (!sfx || sfx->numlinks != last - first + 1) return false;
    for (int i = 0; i < sfx->numlinks; ++i) {
        const sfxinfo_t *member = S_Sfx(sfx->links[i]);
        if (sfx->links[i] != first + i || !member || !member->data) return false;
    }
    return true;
}
static bool named(int id, const char *file) {
    const sfxinfo_t *sfx = S_Sfx(id);
    return sfx && sfx->data && strstr(sfx->name, file);
}
/* The Marine's ranges, read here from units.dat by PyMS's column layout. */
static int marine_ranges(int out[7]) {
    blob_t units = {0};
    CHECK(sc_read(root, "arr/units.dat", &units) && units.size == 19192);
    const int at[7] = {8856, 9068, 9524, 9980, 10192, 10404, 10616};
    for (int i = 0; i < 7; ++i) out[i] = read_u16_le(units.bytes + at[i]);
    W_FreeFile(&units);
    return 0;
}

static int bank(void) {
    int loaded = 0, groups = 0;
    for (int id = 1; S_Sfx(id); ++id) {
        const sfxinfo_t *sfx = S_Sfx(id);
        if (sfx->numlinks) groups++;
        if (sfx->data) loaded++;
    }
    printf("StarCraft: %d native WAV samples and %d sound groups loaded\n", loaded, groups);
    CHECK(loaded > 700 && groups > 100);
    CHECK(named(gameinfo->sound->ui[UI_SOUND_CLICK], "Misc\\Button.wav"));
    CHECK(sc_dialog_click && named(sc_dialog_click, "glue\\mousedown2.wav"));
    int handle = S_StartLocalSound(sc_dialog_click);
    CHECK(handle && S_IsPlaying(handle));
    S_StopChannel(handle);
    return 0;
}

static int briefing_voice(void) {
    const char *map = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
    int count = 0; while (S_Sfx(count + 1)) ++count;
    sc_brief_voice(map, "staredit\\wav\\T1B00tad.wav");
    CHECK(sc_brief_voice_playing());
    sc_brief_voice(map, "staredit\\wav\\T1B01udu.wav");
    CHECK(sc_brief_voice_playing());
    sc_brief_voice(map, "staredit\\wav\\missing.wav");
    CHECK(!sc_brief_voice_playing());
    sc_brief_voice(map, "../staredit/wav/t1b00tad.wav");
    CHECK(!sc_brief_voice_playing());
    for (int i = 0; i < 4; ++i) {
        sc_brief_voice(map, "staredit\\wav\\T1B00tad.wav");
        CHECK(sc_brief_voice_playing()); sc_stop_brief_voice();
        CHECK(!sc_brief_voice_playing());
    }
    CHECK(S_Sfx(count) && !S_Sfx(count + 1));
    return 0;
}

static int responses(void) {
    int r[7];
    CHECK(!marine_ranges(r));
    /* ready, what, pissed and yes, as the retail table has them. */
    CHECK(r[0] == 275 && r[1] == 287 && r[2] == 290 && r[3] == 280 && r[4] == 286 && r[5] == 291 && r[6] == 294);
    mobj_t *marine = spawn(MT_MARINE, 0);
    CHECK(marine);
    CHECK(sound_for(marine, SE_READY) == r[0] && named(r[0], "TMaRdy00"));
    CHECK(rows(sound_for(marine, SE_SELECT), r[1], r[2]) && named(r[1], "TMaWht00"));
    CHECK(rows(sound_for(marine, SE_ACK), r[5], r[6]) && named(r[5], "TMaYes00"));
    /* The same Marine clicked quickly, alone: the pissed rows in order. */
    for (int click = 2; click < 4; ++click) CHECK(rows(sound_for(marine, SE_SELECT), r[1], r[2]));
    CHECK(sound_for(marine, SE_SELECT) == r[3] && named(r[3], "TMaPss00"));
    CHECK(sound_for(marine, SE_SELECT) == r[3] + 1);
    mobj_t *other = spawn(MT_MARINE, 0);
    CHECK(rows(sound_for(other, SE_SELECT), r[1], r[2]));
    /* Groups are shared between units with the same range. */
    CHECK(sound_for(other, SE_ACK) == sound_for(marine, SE_ACK));
    mobj_t *depot = spawn(MT_SUPPLY_DEPOT, 0);
    CHECK(depot && sound_for(depot, SE_SELECT) == 397 && named(397, "tpgwht00") && !sound_for(depot, SE_ACK));
    CHECK(named(sound_for(depot, SE_READY), "TSCUpd00"));
    /* A bark plays the selected local unit's answer. */
    P_MobjSetSelected(marine, true);
    S_Bark(&marine, 1, SE_ACK, true);
    return 0;
}

static int combat(void) {
    mobj_t *marine = spawn(MT_MARINE, 0), *scv = spawn(MT_SCV, 0), *dragoon = spawn(MT_DRAGOON, 0);
    mobj_t *zealot = spawn(MT_ZEALOT, 0), *goliath = spawn(MT_GOLIATH, 0), *wraith = spawn(MT_WRAITH, 0);
    mobj_t *enemy = spawn(MT_ZERGLING, 1), *flyer = spawn(MT_MUTALISK, 1);
    CHECK(marine && scv && dragoon && zealot && goliath && wraith && enemy && flyer && (flyer->traits & MF_FLY));
    marine->attack.target = scv->attack.target = dragoon->attack.target = enemy;
    zealot->attack.target = goliath->attack.target = enemy;
    /* The attack animation's playsnd. */
    CHECK(named(sound_for(marine, SE_ATTACK), "TMaFir00"));
    int fire = S_ActorSound(marine, SE_ATTACK);
    CHECK(fire && S_IsPlaying(fire));
    /* playsndrand before the attackmelee strikes. */
    CHECK(named(S_Sfx(sound_for(zealot, SE_ATTACK))->links[0], "pzeAtt00"));
    /* The Goliath's body fires from its turret's script, per target. */
    CHECK(named(sound_for(goliath, SE_ATTACK), "TGoFir00"));
    /* Silent attack animations fall back to the weapon's bullet. */
    CHECK(rows(sound_for(scv, SE_ATTACK), 35, 39) && named(35, "EDrRep00"));
    CHECK(named(sound_for(dragoon, SE_ATTACK), "DragBull"));
    wraith->attack.target = flyer;
    /* Gemini Missiles' bullet playsndbtwn; Burst Lasers' playsnd. */
    CHECK(rows(sound_for(wraith, SE_ATTACK), 82, 83) && named(82, "TPhFi200"));
    wraith->attack.target = enemy;
    CHECK(sound_for(wraith, SE_ATTACK) == 74 && named(74, "TPhFi100"));
    /* Deaths from the death animation. */
    CHECK(rows(sound_for(marine, SE_DEATH), 276, 277) && named(276, "TMaDth00"));
    CHECK(named(sound_for(enemy, SE_DEATH), "ZZeDth00"));
    mobj_t *cc = spawn(MT_COMMAND_CENTER, 0);
    CHECK(cc && named(sound_for(cc, SE_DEATH), "ExploLrg"));
    /* Warnings: the advisor, once in a while. */
    CHECK(named(sound_for(cc, SE_ATTACKED), "TAdUpd00"));
    CHECK(!sound_for(cc, SE_ATTACKED));
    CHECK(named(sound_for(marine, SE_ATTACKED), "TAdUpd01"));
    /* Research and upgrades end with different advisor lines. */
    CHECK(named(sound_for(cc, SE_RESEARCH_COMPLETE), "TAdUpd06"));
    production_t research = {.product_class = RTS_PRODUCT_UPGRADE, .product_type = SC_TECH_UI, .queue_count = 1};
    cc->production = &research;
    CHECK(named(sound_for(cc, SE_RESEARCH_COMPLETE), "TAdUpd02"));
    cc->production = NULL;
    /* An enemy behind the fog is not heard. */
    uint32_t hidden = 0;
    level.width = level.height = 1;
    level.sight.cells = &hidden;
    CHECK(!S_ActorSound(enemy, SE_DEATH));
    level.sight.cells = NULL;
    level.width = level.height = 0;
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    CHECK(gameinfo->sound == &sc_soundinfo);
    CHECK(S_Init(root));
    RTS_RUN(bank());
    RTS_RUN(briefing_voice());
    RTS_RUN(responses());
    P_FreeThinkers();
    P_InitThinkers();
    RTS_RUN(combat());
    P_FreeThinkers();
    S_Shutdown();
    CHECK(!S_Sfx(1));
    CHECK(S_Init(root)); RTS_RUN(briefing_voice()); S_Shutdown();
    CHECK(!S_Init("/nonexistent/starcraft") && !S_Sfx(1));
    nosound = true;
    CHECK(!S_Init(root));
    nosound = false;
    puts("PASS: StarCraft sfxdata bank, unit responses, annoyed clicks, iscript weapon and death sounds, advisor lines");
    return 0;
}
