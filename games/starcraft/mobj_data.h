#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__
/* Invincible units ignore damage. Hallucinations are excluded from triggers.
 * attacker is the owner of the last hit, valid only when SC_HIT is set. */
enum { SC_INVINCIBLE = 1, SC_HALLUCINATION = 2, SC_HIT = 4 };
#define MOBJ_GAME_FIELDS struct { uint8_t flags, attacker; int guard_hp; } sc;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->sc.flags); HASH((mo)->sc.attacker); HASH((mo)->sc.guard_hp); \
} while (0)
#endif
