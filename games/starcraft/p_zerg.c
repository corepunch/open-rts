#include "sc_local.h"
#include <limits.h>
#include <math.h>
/* The Zerg production model and the ground rules of both alien races.
 * A larva given an order becomes an egg (a mutalisk a cocoon) that carries
 * the order and hatches in place; larvae themselves are p_larva.c.
 * Buildings that need creep or psi check it where they are placed; creep is
 * the per-map tile state of p_creep.c. */
uint16_t sc_egg_for(uint16_t maker) {
    return maker == MT_LARVA ? MT_EGG : maker == MT_MUTALISK ? MT_COCOON : MT_NONE;
}
static bool live(const thinker_t *th) {
    const mobj_t *mo = (const mobj_t *)th;
    return th->function == P_MobjThinker && !mo->remove && mo->hp > 0;
}
/* A larva given an order becomes an egg, a mutalisk a cocoon (p_larva.c keeps
 * the hatchery's timer and the larvae's wandering). */
void sc_zerg_ticker(int elapsed_ms) {
    (void)elapsed_ms;
    sc_larva_ticker();
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (!live(th)) continue;
        mobj_t *mo = (mobj_t *)th;
        if (sc_egg_for(mo->type_id) && mo->production && mo->production->queue_count) {
            if (mo->type_id == MT_LARVA) mo->sc.order = (sc_order_t){0};
            P_MorphMobj(mo, sc_egg_for(mo->type_id));
        }
    }
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
        if (!live(th) || mo->type_id != MT_PYLON || (owner >= 0 && mo->owner != owner) ||
            !G_ModelUnitReady(mo)) continue;
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
bool sc_ground_ok(uint16_t type, ivec2_t cell, int owner) {
    if (type < 1 || type > SC_TYPES) return true;
    uint32_t flags = sc_units[type - 1].flags;
    isize2_t foot = actor_types[type - 1].footprint;
    bool requires_creep=(flags&0x20000)!=0;
    bool is_zerg=(sc_units[type-1].race&1)!=0;
    bool terrain_checks=actor_types[type-1].build_on_type==0;
    const sc_mission_t *m=level.mission;
    for (int y = 0; y < foot.h; ++y) for (int x = 0; x < foot.w; ++x) {
        ivec2_t at=ivec2_add(cell,(ivec2_t){x,y});
        if(requires_creep&&!sc_creep_at(at))return false;
        if(!requires_creep&&terrain_checks&&!is_zerg&&sc_creep_at(at))return false;
        if(requires_creep||!terrain_checks||!m||!m->terrain||!L_Contains(&level,at.x,at.y))continue;
        const sc_terrain_t *tile=&m->terrain[L_Index(&level,at.x,at.y)];
        if(tile->cv5&SC_TERRAIN_UNBUILDABLE)return false;
        int walkable=0;
        for(int i=0;i<16;i++)walkable+=tile->vf4[i]&1;
        if(walkable&&walkable<16)return false;
    }
    if (flags & 0x80000) return in_psi(owner, P_BuildingPosition(type, cell));
    return true;
}
