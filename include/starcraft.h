#ifndef __STARCRAFT__
#define __STARCRAFT__
#include "engine.h"
/* Native indexed GRP decoder; retains every original image and its pivot. */
bool sc_decode_grp(const blob_t *file, const uint32_t *palette, bool turns, spritesheet_t *out);
#define SC_DIALOG_CONTROLS 64
#define SC_CONTROL_MOVIES 4
typedef struct { char path[128]; ivec2_t offset; unsigned flags; } sc_movie_ref_t;
typedef struct {
    int id, type; unsigned flags;
    irect_t rect, hitbox; ivec2_t text_offset;
    char text[128]; int hotkey, mark_at, mark_len;
    sc_movie_ref_t movies[SC_CONTROL_MOVIES]; int movie_count;
} sc_control_t;
typedef struct { irect_t rect; sc_control_t controls[SC_DIALOG_CONTROLS]; int count; } sc_dialog_t;
bool sc_decode_dialog(const blob_t *file, sc_dialog_t *out);
/* 0 while the mission is open, 1 victory, 2 defeat, 3 draw. */
int sc_mission_result(void);
/* One pending Center View, in map cells. */
bool sc_take_camera(fvec2_t *cell);
/* Next numbered campaign CHK relative to the data root, or false on the last map. */
bool sc_campaign_next(const char *map_path, char *out, size_t size);
#endif
