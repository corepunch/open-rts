#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H
/* Compile-time shape of this game's engine build. Shared code tests these
 * capability macros and these level fields; it never names the game. */
#define RTS_MAX_PRODUCTION_QUEUE 9
/* A lingering blast (WEAPON.TXT PersistentDamage): part of the simulation,
 * the lockstep checksum and saved games (p_damage.c). */
#define DR_MAX_EFFECTS 64
typedef struct {
    uint32_t attacker; /* Mobj id, 0 when gone. */
    int32_t x, y;      /* Impact point, 16.16 cells. */
    int16_t strength, area;
    int8_t offense;
    uint8_t owner, remaining, period, wait;
    bool flying;       /* The layer it burst on. */
} dr_effect_t;
#define LEVEL_GAME_FIELDS dr_effect_t dr_effects[DR_MAX_EFFECTS];
#endif
