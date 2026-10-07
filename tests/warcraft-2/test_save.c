#include "t_local.h"
#include "warcraft-2.h"
#include "w2_local.h"

#include <stdlib.h>
#include <sys/stat.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II saves", #c)

static mobj_t *by_id(uint32_t id) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->id == id) return (mobj_t *)th;
    return NULL;
}

int main(void) {
    mkdir("build/test-user", 0777);
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    CHECK(P_LoadThings(NULL) == 64);
    CHECK(P_InitSight());
    mobjlist_t list = P_ListMobjs();
    CHECK(list.count == 64);
    mobj_t *first = list.items[0], *other = list.items[1];
    uint32_t first_id = first->id, other_id = other->id;
    fixed3_t at = first->core.position;
    first->hp = 17;
    first->target = other;
    first->w2.mana = 123;
    first->w2.buffs[W2_BUFF_HASTE] = 456;
    first->w2.cast.spell = W2_SPELL_HASTE;
    first->w2.cast.target = other->id;
    first->w2.repair.target = other->id;
    first->w2.carrier = other->id;
    level.w2_research[consoleplayer] = UINT64_C(1) << W2_UPGRADE_DEATH_AND_DECAY;
    level.player_resources[consoleplayer][0] = 1234;
    leveltime = 321;
    W2_SeedCombat(777);
    W2_SyncRand();
    uint32_t dice = W2_CombatState();
    int count = list.count;
    P_FreeMobjList(&list);
    mobj_t *shot = w2_spawn_effect(first, W2_FX_ARROW, first->core.position);
    CHECK(shot);
    shot->w2.fx.end = other->core.position;
    shot->w2.fx.subject = other->id;
    shot->w2.fx.basic = 12;
    uint32_t shot_id = shot->id;
    ++count;

    app_t app = {.win = {640, 480}, .cam = {12.5f, 30.0f}};
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    hudtext_t hud = {0};
    const char *path = "build/test-user/test.sav";
    CHECK(G_SaveGame(path, "Test", &app, &ai, &hud));
    saveinfo_t info;
    CHECK(G_SaveInfo(path, &info) && !strcmp(info.name, "Test") && strstr(info.map, "ALAMO.PUD"));

    /* Play on, then restore: the thinkers, links, stock and clock come back. */
    first->hp = 1;
    first->target = NULL;
    first->core.position.x += 5 * FIXED_ONE;
    level.player_resources[consoleplayer][0] = 0;
    leveltime = 0;
    W2_SeedCombat(1);
    app.cam = (fvec2_t){0, 0};
    CHECK(G_LoadGame(path, &app, &ai, &hud));
    list = P_ListMobjs();
    CHECK(list.count == count);
    P_FreeMobjList(&list);
    first = by_id(first_id);
    other = by_id(other_id);
    CHECK(first && other && first->hp == 17 && first->target == other);
    CHECK(first->core.position.x == at.x && first->core.position.y == at.y);
    CHECK(first->info && first->thinker.function == P_MobjThinker);
    CHECK(level.player_resources[consoleplayer][0] == 1234 && leveltime == 321);
    CHECK(app.cam.x == 12.5f && app.cam.y == 30.0f);
    CHECK(W2_CombatState() == dice);
    CHECK(first->w2.mana == 123 && first->w2.buffs[W2_BUFF_HASTE] == 456);
    CHECK(first->w2.cast.spell == W2_SPELL_HASTE && first->w2.cast.target == other_id);
    CHECK(first->w2.repair.target == other_id && first->w2.carrier == other_id);
    CHECK(W2_HasResearch(consoleplayer, W2_UPGRADE_DEATH_AND_DECAY));
    shot = by_id(shot_id);
    CHECK(shot && shot->target == first && shot->w2.fx.subject == other_id && shot->w2.fx.basic == 12);

    /* A damaged file is refused rather than misread. */
    FILE *file = fopen(path, "r+b");
    CHECK(file);
    fseek(file, -9, SEEK_END);
    fputc(0x5a, file);
    fclose(file);
    CHECK(!G_SaveInfo(path, &info) && !G_LoadGame(path, &app, &ai, &hud));
    remove(path);
    P_FreeLevel(&level);
    puts("PASS: engine saved games restore mobjs, links, stock, clock and game state, and refuse damage");
    return 0;
}
