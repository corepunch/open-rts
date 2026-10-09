#ifndef __STARCRAFT__
#define __STARCRAFT__
#include "engine.h"
/* Native indexed GRP decoder; retains every original image and its pivot. */
bool sc_decode_grp(const blob_t *file, const uint32_t *palette, bool turns, spritesheet_t *out);
bool sc_movie(const char *path, spritesheet_t *out, unsigned *frame_ms, uint32_t **palettes);
#define SC_DIALOG_CONTROLS 96
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
/* Zerg buildings stand on creep, Protoss ones in a pylon's psi field
 * (units.dat flags 0x20000 and 0x80000). */
bool sc_ground_ok(uint16_t type, ivec2_t cell, const mobj_t *builder);
/* Hatcheries grow larvae, larvae and mutalisks with an order turn into
 * an egg or cocoon, and larvae die with their hatchery. */
void sc_zerg_ticker(int elapsed_ms);
/* A techdata.dat ability (SC_TECH_* in sc_local.h) on target or at a point.
 * Cloaking Field and Personnel Cloaking toggle the caster's cloak for 25
 * energy, then drain energy while it lasts. False when it cannot cast. */
bool sc_cast(mobj_t *caster, int tech, mobj_t *target, fvec2_t at);
/* Current shields and energy in whole points, for the HUD and tests. */
int sc_shields(const mobj_t *mo);
int sc_energy(const mobj_t *mo);
#endif
