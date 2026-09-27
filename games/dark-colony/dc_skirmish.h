#ifndef __DC_SKIRMISH__
#define __DC_SKIRMISH__

#include <stdbool.h>
#include <stdint.h>

/* DC.EXE setup.c: player record +0x1c/+0x20/+0x24/+0x28. */
enum { DC_PLAYER_AI, DC_PLAYER_AI_PLUS, DC_PLAYER_HUMAN, DC_PLAYER_NONE };
typedef struct {
    int race, type, color, team;
    char name[17];
} dc_skirmish_player_t;

typedef struct {
    dc_skirmish_player_t players[8];
    int storage, artifacts, erupting, renewable;
    int flow, quantity, rank; /* Multipliers are native 25% steps, 1..20. */
    uint8_t seed;
} dc_skirmish_t;

/* A start request is consumed once, before the level's SCN is instantiated. */
void DC_RequestSkirmish(const char *map, const dc_skirmish_t *setup);
bool DC_TakeSkirmish(const char *map, dc_skirmish_t *setup);
struct level_s;
const dc_skirmish_t *DC_LevelSkirmish(const struct level_s *map);

#endif
