#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H
/* Compile-time shape of this game's engine build. Shared code tests these
 * capability macros and these level fields; it never names the game. */
#define RTS_MODULE_DARK_COLONY 1
#define RTS_MAX_PRODUCTION_QUEUE 50
#define LEVEL_GAME_FIELDS \
    uint8_t alliance_offers[2][8]; \
    uint32_t peace[8]; \
    struct dc_weapons_s *weapons; \
    struct { uint8_t selected, queued; } purchases[8][110]; /* Native DEPEND rows. */ \
    int exo_income[8]; /* Credits per 16 native ticks while the base stands (team +0xe1c). */
#endif
