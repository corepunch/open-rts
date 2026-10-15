#include "z_local.h"
#include "info.h"

/* Forts fall to whoever gets inside; nothing else ends a level. ZED.EXE
 * keeps no hit points for a fort (0x3ded8 drops the fort building records)
 * and marks the 4x4 cells in its middle with its team's entrance terrain:
 * 0x14, 0x15, 0x3c or 0x3d for teams 0..3. A unit of another team on one
 * of them while the fort stands (0x79c41) makes it fall (0x11fb1 and its
 * three siblings); in a single player game only the first fort does
 * (0x1241f). The fall runs on the 70 Hz clock (0x127a6): if the fort was
 * the player's, the player's units die one a step from the second
 * (0x4613e); on the 17th the fort is a wreck, drawn from the world's
 * second set of pieces (0x12938, 0x12606); on the 170th the level ends
 * (0x12910), lost if the fort was the player's and won otherwise. A fort
 * counts the tics of its fall in z.hold. */

enum {
    NATIVE_HZ = 70,
    FALL_WRECK = 17 * RTS_TICRATE / NATIVE_HZ,
    FALL_END = 170 * RTS_TICRATE / NATIVE_HZ,
};

static const uint8_t entrances[Z_NEUTRAL] = {0x14, 0x15, 0x3c, 0x3d};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

static bool fallen(const mobj_t *fort) {
    return fort->z.hold || fort->core.state_id == S_FORT_WRECK || fort->core.state_id == S_FORT_CPU_WRECK;
}

/* A robot or vehicle of another team whose centre is on the fort's
 * entrance. */
static bool inside(const zmap_t *map, const mobj_t *fort, const mobj_t *unit) {
    if (!live(unit) || Z_UnitType(unit) < 0 || unit->team >= Z_NEUTRAL || unit->team == fort->team) return false;
    ivec2_t cell = fixed2_cell(fixed3_xy(unit->core.position));
    ivec2_t corner = fixed2_footprint_corner(fixed3_xy(fort->core.position), fort->info->footprint);
    if (cell.x < corner.x || cell.y < corner.y || cell.x >= corner.x + fort->info->footprint.w ||
        cell.y >= corner.y + fort->info->footprint.h || cell.x < 0 || cell.y < 0 ||
        cell.x >= Z_MAP_STRIDE || cell.y >= Z_MAP_STRIDE) return false;
    return (map->terrain[cell.y * Z_MAP_STRIDE + cell.x] & 0x7f) == entrances[fort->team];
}

static mobj_t *intruder(const zmap_t *map, mobj_t *const *units, int count, const mobj_t *fort) {
    for (int i = 0; i < count; ++i)
        if (inside(map, fort, units[i])) return units[i];
    return NULL;
}

/* 0x4613e: one of the player's units a native step. */
static void lose_units(mobj_t *const *units, int count, int team, int steps) {
    for (int i = 0; i < count && steps > 0; ++i) {
        mobj_t *mo = units[i];
        if (!live(mo) || Z_UnitType(mo) < 0 || mo->team != team) continue;
        P_DamageMobj(mo, NULL, mo->hp);
        steps--;
    }
}

static void tick_fall(mobj_t *fort, mobj_t *const *units, int count) {
    int t = ++fort->z.hold;
    bool players = fort->owner == consoleplayer;
    if (players) lose_units(units, count, fort->team, t * NATIVE_HZ / RTS_TICRATE - (t - 1) * NATIVE_HZ / RTS_TICRATE);
    if (t == FALL_WRECK)
        P_SetMobjState(fort, fort->core.state_id == S_FORT_CPU ? S_FORT_CPU_WRECK : S_FORT_WRECK);
    if (t >= FALL_END) {
        fort->z.hold = 0;
        G_EndLevel(players ? OUTCOME_DEFEAT : OUTCOME_VICTORY);
    }
}

/* Once a tic, before the engine's elimination rules: the falling fort, or
 * a fort whose entrance someone has reached. While the forts stand they
 * keep both sides alive, so elimination only settles maps without them. */
unsigned G_LevelObjectives(mobj_t *const *units, int count) {
    const zmap_t *map = level.native_data;
    mobj_t *falling = NULL, *entered = NULL;
    bool any_fallen = false;
    for (int i = 0; map && i < count; ++i) {
        mobj_t *fort = units[i];
        if (fort->type_id != MT_FORT || fort->remove || fort->team >= Z_NEUTRAL) continue;
        any_fallen = any_fallen || fallen(fort);
        if (fort->z.hold) falling = fort;
        else if (!entered && !fallen(fort) && intruder(map, units, count, fort)) entered = fort;
    }
    if (falling) tick_fall(falling, units, count);
    else if (entered && !any_fallen) tick_fall(entered, units, count);
    return ELIMINATE_ALL;
}

void Z_FortUnharmed(mobj_t *fort) { fort->hp = fort->max_hp; }

/* The entrance cell nearest the fort's middle. */
bool Z_FortEntrance(const mobj_t *fort, fixed2_t *at) {
    const zmap_t *map = level.native_data;
    if (!map || !fort->info || fort->team >= Z_NEUTRAL) return false;
    fixed2_t centre = fixed3_xy(fort->core.position);
    ivec2_t corner = fixed2_footprint_corner(centre, fort->info->footprint);
    int64_t best = INT64_MAX;
    for (int y = corner.y; y < corner.y + fort->info->footprint.h; ++y)
        for (int x = corner.x; x < corner.x + fort->info->footprint.w; ++x) {
            if (x < 0 || y < 0 || x >= Z_MAP_STRIDE || y >= Z_MAP_STRIDE ||
                (map->terrain[y * Z_MAP_STRIDE + x] & 0x7f) != entrances[fort->team]) continue;
            fixed2_t cell = fixed2_cell_center((ivec2_t){x, y});
            int64_t d2 = fixed2_distance_squared64(cell, centre);
            if (d2 < best) {
                best = d2;
                *at = cell;
            }
        }
    return best != INT64_MAX;
}
