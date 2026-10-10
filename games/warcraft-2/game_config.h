#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H
/* Compile-time shape of this game's engine build. Shared code tests these
 * capability macros and these level fields; it never names the game. */
#define RTS_MODULE_WARCRAFT_2 1
#define RTS_MAX_PRODUCTION_QUEUE 9
#define LEVEL_GAME_FIELDS \
    uint64_t w2_research[8];
#endif
