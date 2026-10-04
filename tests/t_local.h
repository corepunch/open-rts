#ifndef __T_LOCAL__
#define __T_LOCAL__

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "engine.h"


static inline int rts_fail(const char *tag, const char *message) {
    if (tag && tag[0] != '\0') {
        fprintf(stderr, "FAIL (%s): %s\n", tag, message);
    } else {
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return 1;
}

#define RTS_CHECK(cond, tag, msg) \
    do { if (!(cond)) return rts_fail((tag), (msg)); } while (0)

#define RTS_RUN(fn) \
    do { int _r = (fn); if (_r != 0) return _r; } while (0)


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


#define RTS_FIXED_DT (1.0f / 30.0f)

static inline bool rts_tick(RtsGameModel *model, RtsRenderSnapshot *snapshot) {
    if (!rts_game_model_tick(model, RTS_FIXED_DT)) return false;
    if (snapshot && !rts_game_model_snapshot(model, snapshot)) return false;
    return true;
}

static inline bool rts_event_matches(const RtsGameEvent *event, RtsGameEventType wanted,
                                     uint32_t subject_id, int product_type) {
    if (!event) return false;
    if (event->type != wanted) return false;
    if (subject_id != 0 && event->subject_id != subject_id) return false;
    if (product_type >= 0 && event->product_type != product_type) return false;
    return true;
}

static inline bool rts_event_seen(RtsGameModel *model, RtsGameEventType wanted,
                                  uint32_t subject_id, int product_type) {
    RtsGameEvent event;
    while (rts_game_model_poll_event(model, &event)) {
        if (rts_event_matches(&event, wanted, subject_id, product_type)) return true;
    }
    return false;
}

static inline bool rts_tick_until(RtsGameModel *model, RtsRenderSnapshot *snapshot,
                                  int max_ticks, bool (*predicate)(const RtsRenderSnapshot *s, void *user_data),
                                  void *user_data) {
    for (int i = 0; i < max_ticks; ++i) {
        if (!rts_tick(model, snapshot)) return false;
        if (predicate && snapshot && predicate(snapshot, user_data)) return true;
    }
    return false;
}

static inline int rts_find_unit(const RtsRenderSnapshot *snapshot, uint8_t owner,
                                uint16_t type_id) {
    if (!snapshot) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].owner == owner && snapshot->units[i].type_id == type_id)
            return i;
    }
    return -1;
}

static inline int rts_find_unit_by_id(const RtsRenderSnapshot *snapshot, uint32_t id) {
    if (!snapshot || id == 0) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].id == id) return i;
    }
    return -1;
}

static inline int rts_find_unit_with_sprite(const RtsRenderSnapshot *snapshot,
                                            const char *sprite_name) {
    if (!snapshot || !sprite_name) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (strcmp(snapshot->units[i].sprite_name, sprite_name) == 0) return i;
    }
    return -1;
}

static inline int rts_find_active_effect_with_sprite(const RtsRenderSnapshot *snapshot,
                                                     const char *sprite_substring) {
    if (!snapshot || !sprite_substring) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (strstr(snapshot->units[i].sprite_name, sprite_substring))
            return i;
    }
    return -1;
}

static inline int rts_find_decoration_with_sprite(const RtsRenderSnapshot *snapshot,
                                                  const char *sprite_substring) {
    if (!snapshot || !sprite_substring) return -1;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        if (!snapshot->decorations[i].hidden && strstr(snapshot->decorations[i].sprite_name, sprite_substring))
            return i;
    }
    return -1;
}


/* Drive the HUD as the driver does: say what it shows, then hand it the
 * event or the frame. */
static inline bool t_hud_event(menu_t *hud, app_t *app, mobj_t *const *units, int unit_count,
                               const SDL_Event *event) {
    hudview.units = units;
    hudview.unit_count = unit_count;
    return hud && M_MenuResponder(hud, app, event);
}

static inline void t_hud_draw(menu_t *hud, app_t *app, mobj_t *const *units, int unit_count,
                              const spritecache_t *sprites, const hudtext_t *messages) {
    hudview = (hudview_t){.units = units, .unit_count = unit_count,
                         .sprites = sprites, .messages = messages};
    if (!hud) return;
    hud->app = app;
    M_MenuDrawer(hud);
}

#endif
