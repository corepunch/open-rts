#include "engine.h"
#include <stdlib.h>

level_t level;
thinker_t thinkercap;
int leveltime;

void P_InitThinkers(void) {
    thinkercap.prev = thinkercap.next = &thinkercap;
    leveltime = 0;
    level.random_index = 0;
}

void P_AddThinker(thinker_t *thinker) {
    if (!thinkercap.next) P_InitThinkers();
    thinker->prev = thinkercap.prev;
    thinker->next = &thinkercap;
    thinkercap.prev->next = thinker;
    thinkercap.prev = thinker;
}

void P_RemoveThinker(thinker_t *thinker) {
    thinker->function = NULL;
}

void P_RemoveMobj(mobj_t *mobj) {
    if (!mobj) return;
    mobj->remove = true;
    P_RemoveThinker(&mobj->thinker);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *other = (mobj_t *)th;
        if (other->attack.target == mobj) other->attack.target = NULL;
        if (other->target == mobj) other->target = NULL;
        if (other->harvest.base == mobj) other->harvest.base = NULL;
    }
}

static void free_thinker(thinker_t *thinker) {
    thinker->prev->next = thinker->next;
    thinker->next->prev = thinker->prev;
    S_UnlinkMobj((mobj_t *)thinker);
    P_FreeMobjProduction((mobj_t *)thinker);
    free(thinker);
}

void P_RunThinkers(void) {
    if (!thinkercap.next) P_InitThinkers();
    thinker_t *th = thinkercap.next;
    while (th != &thinkercap) {
        if (!th->function) {
            thinker_t *next = th->next;
            free_thinker(th);
            th = next;
        } else {
            th->function((mobj_t *)th);
            th = th->next;
        }
    }
}

void P_FreeThinkers(void) {
    while (thinkercap.next && thinkercap.next != &thinkercap)
        free_thinker(thinkercap.next);
    P_InitThinkers();
}

mobjlist_t P_ListMobjs(void) {
    mobjlist_t list = {0};
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *mobj = (mobj_t *)th;
        if (!mobj->remove) list.count++;
    }
    if (!list.count) return list;
    list.items = malloc((size_t)list.count * sizeof(*list.items));
    if (!list.items) { list.count = 0; return list; }
    int i = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mobj = (mobj_t *)th;
        if (!mobj->remove) list.items[i++] = mobj;
    }
    return list;
}

void P_FreeMobjList(mobjlist_t *list) {
    free(list->items);
    *list = (mobjlist_t){0};
}

void P_Ticker(void) {
    P_SyncBuildingBlocking();
    P_NavRunPlans(&level);
    P_RunThinkers();
    P_SyncDepositStructures(&level);
    P_SeparateUnits(&level);
    /* Advance the native environment clock on cumulative 66 ms boundaries,
     * independently of the engine's 30 Hz thinker clock. */
    int64_t before = (int64_t)leveltime * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
    int64_t clock = ((int64_t)leveltime + 1) * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
    if (clock != before) G_ClockBegin(clock);
    daylight_t *day = &level.daylight;
    if (clock != before && day->duration > 0) {
        if (++day->tics > day->duration) {
            day->tics = 0;
            day->phase = !day->phase;
        }
        if (day->transition > 0 && day->tics <= day->transition) {
            int weight = (int)((int64_t)day->tics * 256 / day->transition);
            day->weight = day->phase ? weight : 256 - weight;
        } else {
            day->weight = day->phase ? 256 : 0;
        }
    }
    G_ClockEnd(before, clock);
    leveltime++;
}
