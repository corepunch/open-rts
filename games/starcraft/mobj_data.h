#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__
/* Invincible units ignore damage. Hallucinations are excluded from triggers.
 * attacker is the owner of the last hit, valid only when SC_HIT is set.
 * SC_STARTED: shields and energy hold their spawn values.
 * SC_LOADED: inside the Bunker that is its parent. SC_RETURNING: an
 * interceptor flying home to its Carrier. SC_HOLD prevents pursuit.
 * parent is what the mobj belongs to: a larva's hatchery, an add-on's
 * building, an interceptor's Carrier, a scarab's Reaver, a loaded unit's
 * Bunker or transport. A hatchery's larva_ms counts down to its next larva.
 * SC_BURROWED: dug in (Burrow, or a Spider Mine waiting), hidden from
 * whoever has no detector, and unable to move. */
enum { SC_INVINCIBLE = 1, SC_HALLUCINATION = 2, SC_HIT = 4, SC_STARTED = 8, SC_LOADED = 16, SC_RETURNING = 32, SC_HOLD = 64,
       SC_BURROWED = 128 };
enum { SC_ORDER_NONE, SC_ORDER_CAST, SC_ORDER_BOARD, SC_ORDER_MERGE, SC_ORDER_NUKE, SC_ORDER_REPAIR,
       SC_ORDER_FOLLOW, SC_ORDER_LIFT, SC_ORDER_LAND, SC_ORDER_MINE, SC_ORDER_BURROW, SC_ORDER_UNBURROW,
       SC_ORDER_INFEST, SC_ORDER_UNLOAD, SC_ORDER_NYDUS };
/* SC timers count 24 Hz frames down to zero; Irradiate and Plague count the
 * 8-frame beats on which they hurt. LIFE ends a timed unit or effect. */
enum { SC_TIMER_LIFE, SC_TIMER_MATRIX, SC_TIMER_IRRADIATE, SC_TIMER_PLAGUE, SC_TIMER_ENSNARE,
       SC_TIMER_LOCKDOWN, SC_TIMER_STORM, SC_TIMER_STIM, SC_TIMER_STASIS,
       SC_TIMER_MAIN_ORDER, SC_TIMER_SPELL_COOLDOWN, SC_TIMER_GROUND_COOLDOWN,
       SC_TIMER_AIR_COOLDOWN, SC_TIMERS };
/* A transport's slots: eight one-slot passengers at most. */
enum { SC_CARGO = 8 };
/* A pending order a unit walks to carry out:
 * a spell or archon merge on target or at a pixel, boarding a Bunker or
 * transport, a nuclear strike being painted, repair, Follow, building
 * flight, laying a Spider Mine, burrowing, infesting a Command Center,
 * unloading at a spot or walking into a Nydus Canal. at holds pixels for
 * spells, mines and unloading, placement cells for landing. time counts frames down
 * (repair debit/Follow check), or marks landing descent. An effect
 * area keeps its spell in tech. */
typedef struct { int kind, tech; uint32_t target; ivec2_t at; int time; } sc_order_t;
enum { SC_QUEUED_ORDERS = 8 };
typedef struct { int order, product; fixed3_t position; uint32_t target; ivec2_t point; int mode; } sc_pending_order_t;
typedef struct { int active, count, owner, harvest_phase, target_owner; bool forced; uint32_t target; sc_pending_order_t pending[SC_QUEUED_ORDERS]; } sc_order_queue_t;
typedef struct { fixed2_t at; uint32_t target; bool set; } sc_rally_t;
typedef struct { uint8_t active, phase; } sc_glow_t;
/* A foundation counts its build time in 24 Hz frames; stage counts the
 * extra frames after the timer ends. worker_hp is the Drone HP a cancel restores. */
typedef struct { int worker_hp, frames, stage; } sc_construction_t;
/* Native flingy speed and turn change independently each 24 Hz frame. */
typedef struct { int weapon, speed, turn, death_state, trail_state; uint32_t previous; bool missed, trail_repeat, trail_emitted; } sc_projectile_t;
/* A running IScript animation (p_iscript.c): program offset, return address,
 * frames still to wait, the animation number; active is 0 or 1. */
typedef struct { uint16_t pc, ret, wait; uint8_t anim, active; } sc_script_t;
/* sprite_owner is the native sprite owner retained when owner becomes 11;
 * owner/team still drive sight and AI, while neutral rendering uses this
 * retained owner for the native player-color remap.
 * Shields, energy and the Defensive Matrix are 1/256 points, as StarCraft
 * keeps them; wound is the fraction of a hit point already lost. parasite
 * holds the sight bits of the teams that see through this unit. hangar
 * counts docked interceptors or scarabs, a silo's nuke or a Vulture's
 * Spider Mines. A Nydus Canal's parent is its other end. */
#define MOBJ_GAME_FIELDS struct { uint8_t flags, attacker, wound, sprite_owner; uint16_t kills; int shields, energy; \
    uint32_t parent; int larva_ms; int timers[SC_TIMERS], matrix, hangar; uint32_t parasite, addon, cargo[SC_CARGO]; \
    sc_order_t order; sc_rally_t rally; sc_glow_t glow; sc_projectile_t projectile; sc_order_queue_t queue; \
    sc_construction_t construction; \
    sc_script_t script; } sc;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->sc.flags); HASH((mo)->sc.attacker); HASH((mo)->sc.sprite_owner); \
    HASH((mo)->sc.wound); HASH((mo)->sc.kills); HASH((mo)->sc.shields); HASH((mo)->sc.energy); \
    HASH((mo)->sc.parent); HASH((mo)->sc.larva_ms); \
    for (int sc_i = 0; sc_i < SC_TIMERS; ++sc_i) HASH((mo)->sc.timers[sc_i]); \
    HASH((mo)->sc.matrix); HASH((mo)->sc.hangar); HASH((mo)->sc.parasite); HASH((mo)->sc.addon); \
    for (int sc_i = 0; sc_i < SC_CARGO; ++sc_i) HASH((mo)->sc.cargo[sc_i]); \
    HASH((mo)->sc.order.kind); HASH((mo)->sc.order.tech); HASH((mo)->sc.order.target); \
    HASH((mo)->sc.order.at.x); HASH((mo)->sc.order.at.y); HASH((mo)->sc.order.time); \
    HASH((mo)->sc.rally.at.x); HASH((mo)->sc.rally.at.y); \
    HASH((mo)->sc.rally.target); HASH((mo)->sc.rally.set); \
    HASH((mo)->sc.glow.active); HASH((mo)->sc.glow.phase); \
    HASH((mo)->sc.projectile.weapon); HASH((mo)->sc.projectile.speed); \
    HASH((mo)->sc.projectile.turn); HASH((mo)->sc.projectile.death_state); HASH((mo)->sc.projectile.trail_state); \
    HASH((mo)->sc.projectile.previous); HASH((mo)->sc.projectile.missed); \
    HASH((mo)->sc.projectile.trail_repeat); HASH((mo)->sc.projectile.trail_emitted); \
    HASH((mo)->sc.script.pc); HASH((mo)->sc.script.ret); HASH((mo)->sc.script.wait); \
    HASH((mo)->sc.script.anim); HASH((mo)->sc.script.active); \
    HASH((mo)->sc.queue.active); HASH((mo)->sc.queue.count); HASH((mo)->sc.queue.owner); HASH((mo)->sc.queue.harvest_phase); HASH((mo)->sc.queue.target_owner); HASH((mo)->sc.queue.forced); HASH((mo)->sc.queue.target); \
    HASH((mo)->sc.construction.worker_hp); HASH((mo)->sc.construction.frames); HASH((mo)->sc.construction.stage); \
    for (int sc_q=0; sc_q<(mo)->sc.queue.count && sc_q<SC_QUEUED_ORDERS; ++sc_q) { \
        const sc_pending_order_t *q=&(mo)->sc.queue.pending[sc_q]; \
        HASH(q->order); HASH(q->product); HASH(q->position.x); HASH(q->position.y); HASH(q->position.z); \
        HASH(q->target); HASH(q->point.x); HASH(q->point.y); HASH(q->mode); \
    } \
} while (0)
#endif
