#include "sc_local.h"
#include <math.h>
/* The Zerg production model and the ground rules of both alien races.
 * A hatchery keeps up to three larvae; a larva given an order becomes an
 * egg (a mutalisk a cocoon) that carries the order and hatches in place.
 * Buildings that need creep or psi check it where they are placed. */
enum {
    SC_MAX_LARVAE = 3,
    SC_LARVA_MS = (342 * 1000 + 23) / 24,  /* Retail: a larva every 342 frames. */
    SC_HATCHERY_CREEP = 10, SC_COLONY_CREEP = 5,
};
static bool hatchery(uint16_t type) { return type == MT_HATCHERY || type == MT_LAIR || type == MT_HIVE; }
uint16_t sc_egg_for(uint16_t maker) {
    return maker == MT_LARVA ? MT_EGG : maker == MT_MUTALISK ? MT_COCOON : MT_NONE;
}
static bool live(const thinker_t *th) {
    const mobj_t *mo = (const mobj_t *)th;
    return th->function == P_MobjThinker && !mo->remove && mo->hp > 0;
}
static int larvae_of(const mobj_t *h) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (live(th) && mo->type_id == MT_LARVA && mo->sc.parent == h->id) ++count;
    }
    return count;
}
/* Larvae sit in a row under the hatchery, left of where workers come out. */
mobj_t *sc_spawn_larva(mobj_t *h) {
    fixed2_t at = fixed3_xy(h->core.position);
    for (int slot = 0; slot < SC_MAX_LARVAE; ++slot) {
        fixed2_t spot = fixed2_add(at, (fixed2_t){-2 * FIXED_ONE + slot * (FIXED_ONE * 3 / 4), FIXED_ONE * 15 / 8});
        bool taken = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap && !taken; th = th->next) {
            const mobj_t *mo = (const mobj_t *)th;
            taken = live(th) && mo->type_id == MT_LARVA && mo->sc.parent == h->id &&
                fixed2_distance_squared64(fixed3_xy(mo->core.position), spot) < FIXED_LIT_64(0.1);
        }
        if (taken) continue;
        mobj_t *larva = sc_spawn_actor(MT_LARVA - 1, (ivec2_t){spot.x >> (FIXED_FRAC_BITS - 5), spot.y >> (FIXED_FRAC_BITS - 5)}, h->owner);
        if (!larva) return NULL;
        larva->team = h->team;
        larva->allegiance = h->allegiance;
        larva->sc.parent = h->id;
        return larva;
    }
    return NULL;
}
void sc_zerg_ticker(int elapsed_ms) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (!live(th)) continue;
        mobj_t *mo = (mobj_t *)th;
        if (hatchery(mo->type_id)) {
            if (larvae_of(mo) >= SC_MAX_LARVAE) { mo->sc.larva_ms = SC_LARVA_MS; continue; }
            if ((mo->sc.larva_ms -= elapsed_ms) > 0) continue;
            mo->sc.larva_ms += SC_LARVA_MS;
            sc_spawn_larva(mo);
        } else if (mo->type_id == MT_LARVA) {
            const mobj_t *h = P_MobjById(mo->sc.parent);
            if (!h || h->remove || h->hp <= 0 || !hatchery(h->type_id)) P_DamageMobj(mo, NULL, mo->hp);
            else if (mo->production && mo->production->queue_count) P_MorphMobj(mo, MT_EGG);
        } else if (sc_egg_for(mo->type_id) && mo->production && mo->production->queue_count) {
            P_MorphMobj(mo, sc_egg_for(mo->type_id));
        }
    }
}

/* Creep is instant and round: radius 10 around a hatchery tier, 5 around
 * a colony, whoever owns it. Retail creep grows tile by tile and recedes. */
static bool on_creep(fixed2_t at) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (!live(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        int radius = hatchery(mo->type_id) ? SC_HATCHERY_CREEP :
            mo->type_id == MT_CREEP_COLONY || mo->type_id == MT_SUNKEN_COLONY ||
            mo->type_id == MT_SPORE_COLONY ? SC_COLONY_CREEP : 0;
        if (radius && fixed2_distance_squared64(fixed3_xy(mo->core.position), at) <= fixed_sq64(FIXED_FROM_INT(radius)))
            return true;
    }
    return false;
}
/* OpenBW's psi field: 16 by 10 cells centred on the pylon, rounded at the
 * corners. A building is powered while its centre lies inside one. */
static const char psi_field[10][17] = {
    "     xxxxxx     ",
    "  xxxxxxxxxxxx  ",
    " xxxxxxxxxxxxxx ",
    "xxxxxxxxxxxxxxxx",
    "xxxxxxxxxxxxxxxx",
    "xxxxxxxxxxxxxxxx",
    "xxxxxxxxxxxxxxxx",
    " xxxxxxxxxxxxxx ",
    "  xxxxxxxxxxxx  ",
    "     xxxxxx     ",
};
static bool in_psi(int owner, fixed2_t at) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (!live(th) || mo->type_id != MT_PYLON || (owner >= 0 && mo->owner != owner)) continue;
        fixed2_t rel = fixed2_sub(at, fixed3_xy(mo->core.position));
        int x = (rel.x + FIXED_FROM_INT(8)) >> FIXED_FRAC_BITS, y = (rel.y + FIXED_FROM_INT(5)) >> FIXED_FRAC_BITS;
        if (x >= 0 && x < 16 && y >= 0 && y < 10 && psi_field[y][x] == 'x') return true;
    }
    return false;
}
bool sc_powered(const mobj_t *mo) {
    return !mo || mo->type_id < 1 || mo->type_id > SC_TYPES || !(sc_units[mo->type_id - 1].flags & 0x80000) ||
        in_psi(mo->owner, fixed3_xy(mo->core.position));
}
/* Without a builder any player's pylon will do. */
bool sc_ground_ok(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    if (type < 1 || type > SC_TYPES) return true;
    uint32_t flags = sc_units[type - 1].flags;
    isize2_t foot = actor_types[type - 1].footprint;
    if (flags & 0x20000)
        for (int y = 0; y < foot.h; ++y)
            for (int x = 0; x < foot.w; ++x)
                if (!on_creep(fixed2_cell_center(ivec2_add(cell, (ivec2_t){x, y})))) return false;
    if (flags & 0x80000) return in_psi(builder ? builder->owner : -1, P_BuildingPosition(type, cell));
    return true;
}
