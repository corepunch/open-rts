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
    fvec2_t at = fixed3_xy_to_fvec2(h->core.position);
    for (int slot = 0; slot < SC_MAX_LARVAE; ++slot) {
        fvec2_t spot = fvec2_add(at, (fvec2_t){-2.0f + slot * 0.75f, 1.875f});
        bool taken = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap && !taken; th = th->next) {
            const mobj_t *mo = (const mobj_t *)th;
            taken = live(th) && mo->type_id == MT_LARVA && mo->sc.parent == h->id &&
                fvec2_distance_squared(fixed3_xy_to_fvec2(mo->core.position), spot) < 0.1f;
        }
        if (taken) continue;
        mobj_t *larva = sc_spawn_actor(MT_LARVA - 1, (ivec2_t){(int)(spot.x * 32), (int)(spot.y * 32)}, h->owner);
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
static bool on_creep(fvec2_t at) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (!live(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        int radius = hatchery(mo->type_id) ? SC_HATCHERY_CREEP :
            mo->type_id == MT_CREEP_COLONY || mo->type_id == MT_SUNKEN_COLONY ||
            mo->type_id == MT_SPORE_COLONY ? SC_COLONY_CREEP : 0;
        if (radius && fvec2_distance_squared(fixed3_xy_to_fvec2(mo->core.position), at) <= radius * radius)
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
static bool in_psi(int owner, fvec2_t at) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (!live(th) || mo->type_id != MT_PYLON || (owner >= 0 && mo->owner != owner)) continue;
        fvec2_t rel = fvec2_sub(at, fixed3_xy_to_fvec2(mo->core.position));
        int x = (int)floorf(rel.x + 8.0f), y = (int)floorf(rel.y + 5.0f);
        if (x >= 0 && x < 16 && y >= 0 && y < 10 && psi_field[y][x] == 'x') return true;
    }
    return false;
}
bool sc_powered(const mobj_t *mo) {
    return !mo || mo->type_id < 1 || mo->type_id > SC_TYPES || !(sc_units[mo->type_id - 1].flags & 0x80000) ||
        in_psi(mo->owner, fixed3_xy_to_fvec2(mo->core.position));
}
/* Without a builder any player's pylon will do. */
bool sc_ground_ok(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    if (type < 1 || type > SC_TYPES) return true;
    uint32_t flags = sc_units[type - 1].flags;
    isize2_t foot = actor_types[type - 1].footprint;
    if (flags & 0x20000)
        for (int y = 0; y < foot.h; ++y)
            for (int x = 0; x < foot.w; ++x)
                if (!on_creep(fvec2_cell_center(ivec2_add(cell, (ivec2_t){x, y})))) return false;
    if (flags & 0x80000) return in_psi(builder ? builder->owner : -1, P_BuildingPosition(type, cell));
    return true;
}
