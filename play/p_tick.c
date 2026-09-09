#include "game.h"
#include "p_local.h"
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
    }
}

static void free_thinker(thinker_t *thinker) {
    thinker->prev->next = thinker->next;
    thinker->next->prev = thinker->prev;
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

static void separate_units(const level_t *map) {
    for (int iter = 0; iter < 3; ++iter) {
        for (thinker_t *tha = thinkercap.next; tha != &thinkercap; tha = tha->next) {
            mobj_t *a = (mobj_t *)tha;
            if (a->remove || a->hp <= 0 || (a->traits & MF_MOBILE) == 0) continue;
            for (thinker_t *thb = tha->next; thb != &thinkercap; thb = thb->next) {
                mobj_t *b = (mobj_t *)thb;
                if (b->remove || b->hp <= 0 || (b->traits & MF_MOBILE) == 0) continue;
                float min_dist = P_MobjRadius(a) + P_MobjRadius(b);
                fvec2_t a_position = fixed3_xy_to_fvec2(a->core.position);
                fvec2_t b_position = fixed3_xy_to_fvec2(b->core.position);
                fvec2_t delta = fvec2_sub(b_position, a_position);
                float dist2 = fvec2_length_squared(delta);
                if (dist2 >= min_dist * min_dist) continue;
                float dist = sqrtf(dist2);
                if (dist < 0.0001f) {
                    float angle = (float)((a->id * 37 + b->id * 17) % 360) * 0.01745329252f;
                    delta = (fvec2_t){ cosf(angle), sinf(angle) };
                    dist = 1.0f;
                }
                float push = (min_dist - dist) * 0.5f;
                fvec2_t separation = fvec2_scale(delta, push / dist);
                fvec2_t separated_a = fvec2_sub(a_position, separation);
                fvec2_t separated_b = fvec2_add(b_position, separation);
                if (P_CheckPosition(map, a, separated_a.x, separated_a.y)) {
                    fixed3_t before = a->core.position;
                    a->core.position = fixed3_with_xy(a->core.position, separated_a);
                    P_ClampToLevel(map, a);
                    a->core.momentum = fixed3_add(
                        a->core.momentum,
                        fixed3_planar_displacement(before, a->core.position));
                }
                if (P_CheckPosition(map, b, separated_b.x, separated_b.y)) {
                    fixed3_t before = b->core.position;
                    b->core.position = fixed3_with_xy(b->core.position, separated_b);
                    P_ClampToLevel(map, b);
                    b->core.momentum = fixed3_add(
                        b->core.momentum,
                        fixed3_planar_displacement(before, b->core.position));
                }
            }
        }
    }
}

static void update_resource_vent_smoke(level_t *map) {
    if (!map || !map->resource_vents) return;
    for (int vent_index = 0; vent_index < map->resource_vent_count; ++vent_index) {
        resourcevent_t *vent = &map->resource_vents[vent_index];
        if (vent->smoke_decoration_index < 0 ||
            vent->smoke_decoration_index >= map->decoration_count) {
            continue;
        }
        bool attached = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *unit = (mobj_t *)th;
            if (!unit->remove && unit->hp > 0 &&
                unit->harvest.target == vent_index &&
                unit->harvest.phase == HARVEST_PHASE_MINING) {
                attached = true;
                break;
            }
        }
        map->decorations[vent->smoke_decoration_index].hidden = !vent->active || attached;
    }
}

void P_Ticker(void) {
    P_RunThinkers();
    separate_units(&level);
    update_resource_vent_smoke(&level);
    leveltime++;
}
