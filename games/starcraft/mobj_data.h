#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__
/* Invincible units ignore damage. Hallucinations are excluded from triggers.
 * attacker is the owner of the last hit, valid only when SC_HIT is set.
 * SC_STARTED: shields and energy hold their spawn values.
 * SC_LOADED: inside the Bunker that is its parent. SC_RETURNING: an
 * interceptor flying home to its Carrier.
 * parent is what the mobj belongs to: a larva's hatchery, an add-on's
 * building, an interceptor's Carrier, a scarab's Reaver, a loaded unit's
 * Bunker. A hatchery's larva_ms counts down to its next larva. */
enum { SC_INVINCIBLE = 1, SC_HALLUCINATION = 2, SC_HIT = 4, SC_STARTED = 8, SC_LOADED = 16, SC_RETURNING = 32 };
/* Spell timers count 24 Hz frames down to zero; Irradiate and Plague count
 * the 8-frame beats on which they hurt. LIFE ends a timed unit or effect. */
enum { SC_TIMER_LIFE, SC_TIMER_MATRIX, SC_TIMER_IRRADIATE, SC_TIMER_PLAGUE, SC_TIMER_ENSNARE,
       SC_TIMER_LOCKDOWN, SC_TIMER_STORM, SC_TIMERS };
/* A pending order a unit walks to carry out (SC_ORDER_* in sc_local.h):
 * a spell or archon merge on target or at a pixel, boarding a Bunker, a
 * nuclear strike being painted. time counts its frames down. An effect
 * area keeps its spell in tech. */
typedef struct { int kind, tech; uint32_t target; ivec2_t at; int time; } sc_order_t;
/* Shields, energy and the Defensive Matrix are 1/256 points, as StarCraft
 * keeps them; wound is the fraction of a hit point already lost. parasite
 * holds the sight bits of the teams that see through this unit. hangar
 * counts docked interceptors or scarabs, or a silo's nuke. */
#define MOBJ_GAME_FIELDS struct { uint8_t flags, attacker, wound; int guard_hp, shields, energy; \
    uint32_t parent; int larva_ms; int timers[SC_TIMERS], matrix, hangar; uint32_t parasite, addon, cargo[4]; \
    sc_order_t order; } sc;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->sc.flags); HASH((mo)->sc.attacker); HASH((mo)->sc.guard_hp); \
    HASH((mo)->sc.wound); HASH((mo)->sc.shields); HASH((mo)->sc.energy); \
    HASH((mo)->sc.parent); HASH((mo)->sc.larva_ms); \
    for (int sc_i = 0; sc_i < SC_TIMERS; ++sc_i) HASH((mo)->sc.timers[sc_i]); \
    HASH((mo)->sc.matrix); HASH((mo)->sc.hangar); HASH((mo)->sc.parasite); HASH((mo)->sc.addon); \
    for (int sc_i = 0; sc_i < 4; ++sc_i) HASH((mo)->sc.cargo[sc_i]); \
    HASH((mo)->sc.order.kind); HASH((mo)->sc.order.tech); HASH((mo)->sc.order.target); \
    HASH((mo)->sc.order.at.x); HASH((mo)->sc.order.at.y); HASH((mo)->sc.order.time); \
} while (0)
#endif
