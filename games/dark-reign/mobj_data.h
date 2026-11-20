#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* Amper boost (dkreign.exe unit +0x104..+0x110): state 0 none, 1 holding
 * for `timer` cycles, 2 fading by an 8.8 `rate` with `timer` as the
 * accumulator, until hp reaches `floor`. Saved with the mobj. */
#define MOBJ_GAME_FIELDS \
    struct { uint8_t boost; int16_t timer; uint16_t rate; int16_t floor; } dr;

#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->dr.boost); HASH((mo)->dr.timer); HASH((mo)->dr.rate); HASH((mo)->dr.floor); \
} while (0)

#endif
