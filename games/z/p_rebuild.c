#include "z_local.h"
#include "info.h"

/* The four death routines build their recorded layers at these pixel offsets.
 * ZED.EXE 0x3ef32, 0x3f158, 0x3f448 and 0x3f7bb. */
static const ivec2_t repair[] = {
    {61,16},{61,32},{18,5},{16,32},{6,16},{32,0},{16,0},{16,32},{0,16},{32,32}
};
static const ivec2_t tank[] = {
    {16,64},{14,48},{13,63},{12,70},{22,55},{10,15},{0,16},{8,32},
    {0,16},{0,16},{0,64},{0,48}
};
static const ivec2_t robot[] = {
    {13,29},{38,55},{14,47},{10,14},{16,64},{26,84},{23,84},{20,84},
    {17,84},{14,84},{11,84},{34,30},{38,30},{42,30},{46,30},{50,30},
    {54,30},{0,16},{16,48},{0,16},{0,16},{24,56},{0,48}
};
static const ivec2_t radar[] = {
    {18,14},{16,18},{41,0},{16,22},{32,0},{16,0},{16,32},{0,16}
};

static int kind(const mobj_t *mo) {
    switch (mo->type_id) {
    case MT_REPAIR: return 0;
    case MT_TANK_FACTORY: return 1;
    case MT_ROBOT_FACTORY: return 2;
    case MT_RADAR: return 3;
    default: return -1;
    }
}

void Z_BuildingDestroyed(mobj_t *building) {
    zmap_t *map = level.native_data;
    int k = kind(building);
    if (!map || k < 0) return;
    static const ivec2_t *const offsets[] = {repair, tank, robot, radar};
    static const size_t counts[] = {sizeof(repair)/sizeof(*repair), sizeof(tank)/sizeof(*tank),
                                    sizeof(robot)/sizeof(*robot), sizeof(radar)/sizeof(*radar)};
    ivec2_t origin = Z_BuildingOrigin(building);
    for (size_t i = 0; i < counts[k]; ++i) {
        mobj_t *p = P_SpawnMobj((fixed3_t){0}, MT_BUILDING_DEBRIS);
        if (!p) break;
        uint16_t a = Z_NextRandom(&map->rebuild.seed);
        uint16_t b = Z_NextRandom(&map->rebuild.seed);
        (void)Z_NextRandom(&map->rebuild.seed); /* burst8 rotation */
        p->z.debris.kind = (uint8_t)k;
        p->z.debris.origin = ivec2_add(origin, ivec2_sub(offsets[k][i], (ivec2_t){160,92}));
        p->z.debris.velocity = (ivec2_t){(a & 31) - 16, ((a >> 8) & 31) - 16};
        p->z.debris.rise = -(int)(b & 3) - 4;
    }
}

/* 0x6e25f uses a fixed camera set by 0x6d3b2. SINCOS.DAT entries were
 * byte-swapped and shifted by 0x70c24 at level load. */
static ivec2_t project(const zmap_t *map, const mobj_t *p) {
    int x = (int16_t)((int16_t)p->z.debris.position.x - 1);
    int y = (int16_t)((int16_t)p->z.debris.position.y - 471);
    int z = (int16_t)((int16_t)p->z.debris.height + 256);
    int c = map->debris_trig[0], s = map->debris_trig[1];
    int depth = (int16_t)((z * s >> 10) + (y * c >> 10));
    int vertical = (int16_t)((y * s >> 10) - (z * c >> 10));
    if (!depth) depth = 1; /* 0x6e382..0x6e3a3 */
    ivec2_t result = ivec2_add((ivec2_t){(int16_t)p->z.debris.origin.x, (int16_t)p->z.debris.origin.y},
                               (ivec2_t){160 + x * 256 / depth, 100 + vertical * 256 / depth});
    return (ivec2_t){(int16_t)result.x, (int16_t)result.y};
}

static void record_step(zmap_t *map, mobj_t *p) {
    if (p->z.debris.kind >= sizeof(map->rebuild.frames)) {
        P_RemoveMobj(p);
        return;
    }
    /* burst8 starts with lifetime 400; 0x6d43b decrements it before
     * recording and killpix runs when it becomes negative. */
    if (p->z.debris.ticks >= 400) {
        map->rebuild.frames[p->z.debris.kind] = p->z.debris.samples;
        P_RemoveMobj(p);
        return;
    }
    ivec2_t at = project(map, p);
    if (p->z.debris.ticks & 1) ++p->z.debris.samples;
    p->z.debris.position = (ivec2_t){
        (int16_t)((int16_t)p->z.debris.position.x + (int8_t)p->z.debris.velocity.x),
        (int16_t)((int16_t)p->z.debris.position.y + (int8_t)p->z.debris.velocity.y)};
    if (at.x < -32 || at.y < -32 || at.x >= (level.width / 2) * 32 || at.y >= (level.height / 2) * 32) {
        P_RemoveMobj(p); /* 0x6e0e6 bypasses killpix and its descriptor write. */
        return;
    }
    p->z.debris.height = (int16_t)((int16_t)p->z.debris.height + (int8_t)p->z.debris.rise);
    ++p->z.debris.ticks;
    if (p->z.debris.height > 0) {
        map->rebuild.frames[p->z.debris.kind] = p->z.debris.samples;
        P_RemoveMobj(p);
        return;
    }
    if (p->z.debris.ticks % 5 == 0) {
        ivec2_t *v = &p->z.debris.velocity;
        v->x -= (v->x > 0) - (v->x < 0);
        v->y -= (v->y > 0) - (v->y < 0);
        p->z.debris.rise = (int8_t)((int8_t)p->z.debris.rise + 1);
    }
}

static void restore(mobj_t *building) {
    building->hp = building->max_hp;
    building->z.rebuild_stock = building->z.rebuild_setting == UINT16_MAX ? -1 : building->z.rebuild_setting * 1700;
    building->traits |= MF_SELECTABLE;
    building->z.gun_count = 0;
    P_FreeMobjProduction(building);
    P_SetMobjState(building, mobjinfo[building->type_id].spawnstate);
}

bool Z_RebuildSlotAvailable(void) {
    int active = 0;
    /* 0x24cc0 searches 64 shared slots. Bridge reconstruction is the other
     * implemented user of these effects; they remain ordinary mobjs. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *mo = (const mobj_t *)th;
        if (!mo->remove && (mo->type_id == MT_BUILDING_REBUILD || mo->core.state_id == S_BRIDGE_REBUILD)) ++active;
    }
    return active < 64;
}

bool Z_StartBuildingRebuild(mobj_t *building) {
    zmap_t *map = level.native_data;
    int k = kind(building);
    if (!Z_RebuildSlotAvailable()) return false;
    mobj_t *effect = map && k >= 0 ? P_SpawnMobj((fixed3_t){0}, MT_BUILDING_REBUILD) : NULL;
    if (!effect) return false;
    effect->z.debris.samples = map->rebuild.frames[k];
    effect->z.service = building->id;
    return true;
}

void Z_RebuildTicker(void) {
    zmap_t *map = level.native_data;
    for (int n = Z_NativeSteps(leveltime, Z_NATIVE_HZ); n-- > 0;) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo->remove || !mo->z.building) continue;
            if (mo->hp <= 0 && mo->z.rebuild_stock >= 0) {
                int total = 0, held = map->rebuild.owner < 4 ? Z_TerritoriesHeld(map->rebuild.owner, &total) : 0;
                int percent = total ? held * 100 / total : 0;
                int extra = percent - (netgame ? 12 : 25);
                if (extra < 0) extra = 0;
                if (extra > 75) extra = 75;
                mo->z.rebuild_stock -= 100 + extra;
                if (mo->z.rebuild_stock < 0) {
                    Z_StartBuildingRebuild(mo);
                    mo->z.rebuild_stock = mo->z.rebuild_setting * 1700;
                }
            }
            map->rebuild.owner = mo->team < 4 ? mo->team : 4;
        }
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo->remove || mo->type_id != MT_BUILDING_REBUILD) continue;
            if (mo->z.debris.samples-- == 2) {
                mobj_t *target = Z_ServiceTarget(mo->z.service);
                if (target) {
                    restore(target);
                    if (netgame) map->rebuild.owner = target->team < 4 ? target->team : 4;
                }
                P_RemoveMobj(mo);
            }
        }
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (!mo->remove && mo->type_id == MT_BUILDING_DEBRIS) record_step(map, mo);
        }
    }
}
