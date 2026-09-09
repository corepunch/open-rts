#ifndef __MOBJ_TEST__
#define __MOBJ_TEST__

#include "game.h"
#include <assert.h>

static inline void copy_mobj_fixture(mobj_t *actor, const mobj_t *value) {
    thinker_t thinker = actor->thinker;
    uint32_t id = actor->id;
    *actor = *value;
    actor->thinker = thinker;
    actor->id = id;
}

static inline mobj_t *spawn_mobj_fixture(mobj_t value) {
    mobj_t *actor = P_SpawnMobj(value.core.position, value.type_id);
    assert(actor);
    copy_mobj_fixture(actor, &value);
    return actor;
}

#endif
