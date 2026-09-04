#ifndef __RTS_MODEL_TEST_H__
#define __RTS_MODEL_TEST_H__

#include "rts_test.h"
#include "g_game.h"
#include <stdbool.h>
#include <string.h>

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
    for (int i = 0; i < snapshot->effect_count; ++i) {
        if (snapshot->effects[i].active && strstr(snapshot->effects[i].sprite_name, sprite_substring))
            return i;
    }
    return -1;
}

static inline bool rts_has_active_ground_light(const RtsRenderSnapshot *snapshot) {
    if (!snapshot) return false;
    for (int i = 0; i < snapshot->effect_count; ++i) {
        if (snapshot->effects[i].active && snapshot->effects[i].ground_light) return true;
    }
    return false;
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

#endif
