#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__
/* Invincible units ignore damage. Hallucinations are excluded from triggers.
 * attacker is the owner of the last hit, valid only when SC_HIT is set.
 * SC_STARTED: shields and energy hold their spawn values.
 * A larva's parent is its hatchery's id; a hatchery's larva_ms counts
 * down to its next larva. */
enum { SC_INVINCIBLE = 1, SC_HALLUCINATION = 2, SC_HIT = 4, SC_STARTED = 8 };
/* Shields and energy are 1/256 points, as StarCraft keeps them; wound is
 * the fraction of a hit point already lost. */
#define MOBJ_GAME_FIELDS struct { uint8_t flags, attacker, wound; int guard_hp, shields, energy; \
    uint32_t parent; int larva_ms; } sc;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->sc.flags); HASH((mo)->sc.attacker); HASH((mo)->sc.guard_hp); \
    HASH((mo)->sc.wound); HASH((mo)->sc.shields); HASH((mo)->sc.energy); \
    HASH((mo)->sc.parent); HASH((mo)->sc.larva_ms); \
} while (0)
#endif
