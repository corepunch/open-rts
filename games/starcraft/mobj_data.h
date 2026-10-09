#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__
/* Invincible units ignore damage. Hallucinations are excluded from triggers.
 * attacker is the owner of the last hit, valid only when SC_HIT is set.
 * A larva's parent is its hatchery's id; a hatchery's larva_ms counts
 * down to its next larva. */
enum { SC_INVINCIBLE = 1, SC_HALLUCINATION = 2, SC_HIT = 4 };
#define MOBJ_GAME_FIELDS struct { uint8_t flags, attacker; int guard_hp; uint32_t parent; int larva_ms; } sc;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->sc.flags); HASH((mo)->sc.attacker); HASH((mo)->sc.guard_hp); \
    HASH((mo)->sc.parent); HASH((mo)->sc.larva_ms); \
} while (0)
#endif
