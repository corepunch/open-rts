#ifndef OPEN_RTS_DARK_COLONY_TYPES_H
#define OPEN_RTS_DARK_COLONY_TYPES_H

#include <stddef.h>
#include <stdint.h>

enum {
    MT_DC_BUILDING_BASE = 1000,
    MT_DC_EXCOPOD = MT_DC_BUILDING_BASE,
    MT_DC_BRRKPOD,
    MT_DC_ROBOPOD,
    MT_DC_ROBOPOD2,
    MT_DC_SCNCPOD,
    MT_DC_SCNCPOD2,
    MT_DC_RSCHPOD,
    MT_DC_ALIEN_MINDHIVE,
    MT_DC_ALIEN_WARHIVE,
    MT_DC_ALIEN_BRDRHIVE,
    MT_DC_ALIEN_BRDRHIVE2,
    MT_DC_ALIEN_MINDHIVE2,
    MT_DC_ALIEN_MINDHIVE3,
    MT_DC_ALIEN_RSCHIVE,
    MT_DC_COMMS_DISH,
    MT_DC_CITY_TOWER,
};

enum {
    DC_MAX_OBJECTS = 800,
    DC_BUILDINGS_PER_SIDE = 15,
    DC_BUILDING_OBJECT_COUNT = 8 * DC_BUILDINGS_PER_SIDE,
    DC_DYNAMIC_OBJECT_FIRST = 0x98,
    DC_OBJECT_SIZE = 0xdc,
    DC_OBJECT_TYPE_PETRA7_VENT = 40,
    DC_FIXED_TILE_CENTER = 0x80,
};

typedef struct {
    int16_t x_pos;              /* +0x00, 8.8 fixed map x */
    int16_t unk_02;             /* +0x02 */
    int16_t z_pos;              /* +0x04, 8.8 fixed map z */
    uint8_t type;               /* +0x06, GAMESTAT object type */
    uint8_t team;               /* +0x07 */
    uint8_t unk_08;             /* +0x08 */
    uint8_t stat_byte;          /* +0x09, copied from the object type table */
    uint8_t regen_timer;        /* +0x0a, initialized to 0x40 */
    uint8_t unk_0b;             /* +0x0b */
    int32_t health_or_amount;   /* +0x0c, health for units/buildings, P-7 amount for vents */
    uint8_t anim_flags;         /* +0x10 */
    uint8_t unk_11;             /* +0x11 */
    uint8_t unk_12;             /* +0x12 */
    uint8_t unk_13;             /* +0x13 */
    uint8_t anim_state0[8];     /* +0x14 */
    uint8_t anim_state1[8];     /* +0x1c */
    uint8_t anim_state2[8];     /* +0x24 */
    uint8_t active;             /* +0x2c, 0 = free/inactive, 10 observed as destroyed/burned */
    uint8_t pad_2d[0x32 - 0x2d];
    int16_t vent_rate;          /* +0x32, resource rate for type-40 Petra-7 vents */
    uint8_t unk_34;             /* +0x34 */
    uint8_t facing_or_anim;     /* +0x35, initialized to 0xff */
    uint8_t unk_36;             /* +0x36 */
    uint8_t pad_37[0xc6 - 0x37];
    uint8_t unk_c6;             /* +0xc6 */
    uint8_t unk_c7;             /* +0xc7 */
    uint8_t unk_c8;             /* +0xc8 */
    uint8_t unk_c9;             /* +0xc9 */
    uint8_t unk_ca;             /* +0xca */
    uint8_t subtype;            /* +0xcb */
    uint8_t unk_cc;             /* +0xcc */
    uint8_t cell_x;             /* +0xcd, integer map x */
    uint8_t cell_z;             /* +0xce, integer map z */
    uint8_t unk_cf;             /* +0xcf */
    uint8_t cooldown_d0;        /* +0xd0 */
    uint8_t marker_d1;          /* +0xd1, initialized to 0xff */
    int16_t target_a;           /* +0xd2, initialized to -2 */
    int16_t target_b;           /* +0xd4, initialized to -2 */
    uint8_t timer_d6;           /* +0xd6 */
    uint8_t pad_d7[DC_OBJECT_SIZE - 0xd7];
} DcObject;

typedef struct {
    DcObject objects[DC_MAX_OBJECTS];
    uint16_t active_objects[DC_MAX_OBJECTS];
    int object_limit;
    int active_count;
} DcObjectPool;

_Static_assert(sizeof(DcObject) == DC_OBJECT_SIZE, "DcObject must match DC.EXE stride");
_Static_assert(offsetof(DcObject, x_pos) == 0x00, "DcObject.x_pos offset");
_Static_assert(offsetof(DcObject, z_pos) == 0x04, "DcObject.z_pos offset");
_Static_assert(offsetof(DcObject, type) == 0x06, "DcObject.type offset");
_Static_assert(offsetof(DcObject, team) == 0x07, "DcObject.team offset");
_Static_assert(offsetof(DcObject, health_or_amount) == 0x0c, "DcObject.health offset");
_Static_assert(offsetof(DcObject, active) == 0x2c, "DcObject.active offset");
_Static_assert(offsetof(DcObject, vent_rate) == 0x32, "DcObject.vent_rate offset");
_Static_assert(offsetof(DcObject, cell_x) == 0xcd, "DcObject.cell_x offset");
_Static_assert(offsetof(DcObject, cell_z) == 0xce, "DcObject.cell_z offset");
_Static_assert(offsetof(DcObject, target_a) == 0xd2, "DcObject.target_a offset");
_Static_assert(offsetof(DcObject, target_b) == 0xd4, "DcObject.target_b offset");

#endif
