#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#include <SDL.h>

#define CHECK(c) RTS_CHECK(c, "StarCraft initial states", #c)

static mobj_t *spawn(int type) {
    mobj_t *mo = P_SpawnMobj((fixed3_t){FIXED_FROM_INT(8), FIXED_FROM_INT(8), 0}, (uint16_t)type);
    if (mo) {
        mo->owner = mo->team = 0;
        mo->allegiance = ALLEGIANCE_PLAYER;
    }
    return mo;
}

static void put_u16(uint8_t *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void put_u32(uint8_t *p, unsigned value) { put_u16(p, value); put_u16(p + 2, value >> 16); }
static int append_chunk(FILE *file, const char *tag, const void *data, size_t size) {
    uint8_t header[8]; memcpy(header, tag, 4); put_u32(header + 4, (unsigned)size);
    return fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
           fwrite(data, 1, size, file) != size;
}

static int write_unit_map(const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 1;
    uint8_t header[8], dim[4], era[2] = {0}, owners[12] = {6, 5},
        terrain[32 * 32 * 2] = {0}, units[2 * 36] = {0};
    put_u16(dim, 32); put_u16(dim + 2, 32);
    const struct { const char *tag; const void *data; size_t size; } chunks[] = {
        {"DIM ", dim, sizeof(dim)}, {"ERA ", era, sizeof(era)},
        {"MTXM", terrain, sizeof(terrain)}, {"OWNR", owners, sizeof(owners)},
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof(chunks) / sizeof(*chunks); i++) {
        memcpy(header, chunks[i].tag, 4); put_u32(header + 4, (unsigned)chunks[i].size);
        failed |= fwrite(header, 1, sizeof(header), file) != sizeof(header);
        failed |= fwrite(chunks[i].data, 1, chunks[i].size, file) != chunks[i].size;
    }
    uint8_t *ghost = units, *ling = units + 36;
    put_u16(ghost + 4, 160); put_u16(ghost + 6, 160); put_u16(ghost + 8, MT_GHOST - 1);
    put_u16(ghost + 12, 1); put_u16(ghost + 26, 1);
    put_u16(ling + 4, 256); put_u16(ling + 6, 160); put_u16(ling + 8, MT_ZERGLING - 1);
    put_u16(ling + 12, 2); put_u16(ling + 26, 2);
    failed |= append_chunk(file, "UNIT", units, sizeof(units));
    uint8_t trigger[2400] = {0}, property[20] = {0};
    trigger[15] = 22; trigger[2372 + 17] = 1;
    uint8_t *action = trigger + 320;
    put_u32(action + 16, 1); put_u32(action + 20, 1); put_u16(action + 24, MT_DEFILER - 1);
    action[26] = 11; action[27] = 1; action[28] = 28;
    put_u16(property, 2); put_u16(property + 14, 2);
    failed |= append_chunk(file, "TRIG", trigger, sizeof(trigger));
    failed |= append_chunk(file, "UPRP", property, sizeof(property));
    failed |= ferror(file);
    failed |= fclose(file) != 0;
    return failed;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    level.width = level.height = 32;
    level.blocked = calloc(32 * 32, 1);
    level.cell_solid = calloc(32 * 32, 1);
    level.mission = calloc(1, sizeof(sc_mission_t));
    CHECK(level.blocked && level.cell_solid && level.mission && P_InitSight());

    /* PyMS CHK UNIT and UPRP share valid-ability/state bits 0 and 1. */
    mobj_t *ghost = spawn(MT_GHOST);
    CHECK(ghost && !(ghost->traits & MF_CLOAKED));
    sc_initial_ability_state(ghost, 1, 1);
    CHECK((ghost->traits & MF_CLOAKED) && (ghost->sc.flags & SC_STARTED));
    CHECK(sc_energy(ghost) == 50);

    mobj_t *ling = spawn(MT_ZERGLING);
    CHECK(ling && (ling->traits & MF_MOBILE) && (ling->traits & MF_ATTACK));
    sc_initial_ability_state(ling, 2, 2);
    CHECK((ling->sc.flags & SC_BURROWED) && (ling->traits & MF_CLOAKED));
    CHECK(!(ling->traits & (MF_MOBILE | MF_ATTACK)) && ling->sc.order.kind == SC_ORDER_NONE);

    mobj_t *defiler = spawn(MT_DEFILER);
    CHECK(defiler);
    sc_initial_ability_state(defiler, 2, 2);
    for (int frame = 0; frame < 24; frame++) P_Ticker();
    CHECK((defiler->sc.flags & SC_BURROWED) && (defiler->traits & MF_CLOAKED));
    CHECK(sc_energy(defiler) >= 50); /* Passive burrow concealment does not drain. */

    mobj_t *marine = spawn(MT_MARINE);
    CHECK(marine);
    sc_initial_ability_state(marine, 3, 0); /* Ability allowed, state not active. */
    CHECK(!(marine->traits & MF_CLOAKED) && !(marine->sc.flags & SC_BURROWED));
    sc_initial_ability_state(marine, 0, 3); /* State without valid ability. */
    CHECK(!(marine->traits & MF_CLOAKED) && !(marine->sc.flags & SC_BURROWED));

    /* Retail CanUseTech (0x4019f0) gates Cloak, Decloak, Burrow and Unburrow alike:
     * research or hero, so CHK-granted states stay stuck without research. */
    CHECK(!sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0})); /* Decloak refused. */
    CHECK(ghost->traits & MF_CLOAKED);
    CHECK(!sc_cast(ling, SC_TECH_BURROWING, NULL, (fixed2_t){0})); /* Unburrow refused. */
    CHECK(ling->sc.flags & SC_BURROWED);
    level.upgrades[SC_UPGRADES + SC_TECH_PERSONNEL_CLOAKING][0].weapon = 1;
    level.upgrades[SC_UPGRADES + SC_TECH_BURROWING][0].weapon = 1;
    ghost->sc.timers[SC_TIMER_LOCKDOWN] = 100;
    CHECK(!sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0})); /* Disabled. */
    ghost->sc.timers[SC_TIMER_LOCKDOWN] = 0;
    CHECK(sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && !(ghost->traits & MF_CLOAKED));
    ghost->sc.energy = 24 << 8; /* Cloak costs 25: one short fails, exactly 25 passes. */
    CHECK(!sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && !(ghost->traits & MF_CLOAKED));
    ghost->sc.energy = 25 << 8;
    CHECK(sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && (ghost->traits & MF_CLOAKED));
    CHECK(sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && !(ghost->traits & MF_CLOAKED)); /* Decloak is free. */
    CHECK(sc_cast(ling, SC_TECH_BURROWING, NULL, (fixed2_t){0}) && ling->sc.order.kind == SC_ORDER_UNBURROW);
    level.upgrades[SC_UPGRADES + SC_TECH_PERSONNEL_CLOAKING][0].weapon = 0;
    level.upgrades[SC_UPGRADES + SC_TECH_BURROWING][0].weapon = 0;
    mobj_t *hero = spawn(MT_SARAH_KERRIGAN);
    CHECK(hero);
    sc_initial_ability_state(hero, 1, 1);
    CHECK(sc_cast(hero, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && !(hero->traits & MF_CLOAKED)); /* Heroes need no research. */

    /* Exercise the retail UNIT parser and loaded Burrow iscript, not just the helper. */
    P_FreeThinkers(); P_InitThinkers(); P_FreeLevel(&level);
    const char *map = "/private/tmp/open-rts-sc-initial-unit.chk";
    CHECK(!write_unit_map(map));
    CHECK(G_DoLoadLevel(map, &level) && G_LoadLevelData("data/STARCRAFT", &level) && P_InitSight());
    CHECK(P_LoadThings(level.map_path) == 2);
    mobj_t *placed_ghost = NULL, *placed_ling = NULL;
    mobjlist_t placed = P_ListMobjs();
    for (int i = 0; i < placed.count; i++) {
        if (placed.items[i]->type_id == MT_GHOST) placed_ghost = placed.items[i];
        if (placed.items[i]->type_id == MT_ZERGLING) placed_ling = placed.items[i];
    }
    spritecache_t cache = {0};
    CHECK(R_InitSprites("data/STARCRAFT", &level, placed.items, placed.count, &cache));
    CHECK(placed_ghost && placed_ling && (placed_ghost->traits & MF_CLOAKED));
    CHECK((placed_ling->sc.flags & SC_BURROWED) && (placed_ling->traits & MF_CLOAKED));
    int ling_head = sc_burrow_states[MT_ZERGLING - 1][0];
    int ling_final = sc_burrow_final_states[MT_ZERGLING - 1];
    CHECK(ling_head && ling_final && ling_head != ling_final);
    CHECK(placed_ling->core.state_id == ling_final && placed_ling->core.state_id != ling_head);
    P_FreeMobjList(&placed);
    for (int frame = 0; frame < 24; frame++) P_Ticker();
    CHECK((placed_ling->sc.flags & SC_BURROWED) && (placed_ling->traits & MF_CLOAKED));
    CHECK(sc_energy(placed_ghost) <= 50 && (placed_ghost->traits & MF_CLOAKED));
    hudtext_t hud = {0}; mobjlist_t all = P_ListMobjs(); int count = all.count;
    G_MissionTicker(&level, all.items, &count, &hud, RTS_TICK_MS); P_FreeMobjList(&all);
    all = P_ListMobjs(); mobj_t *trigger_defiler = NULL;
    for (int i = 0; i < all.count; i++)
        if (all.items[i]->type_id == MT_DEFILER) trigger_defiler = all.items[i];
    CHECK(trigger_defiler && (trigger_defiler->sc.flags & SC_BURROWED));
    CHECK(!(trigger_defiler->traits & (MF_MOBILE | MF_ATTACK)) && (trigger_defiler->traits & MF_CLOAKED));
    int defiler_head = sc_burrow_states[MT_DEFILER - 1][0];
    int defiler_final = sc_burrow_final_states[MT_DEFILER - 1];
    CHECK(defiler_head && defiler_final && defiler_head != defiler_final);
    CHECK(trigger_defiler->core.state_id == defiler_final && trigger_defiler->core.state_id != defiler_head);
    P_FreeMobjList(&all);
    uint32_t ling_id = placed_ling->id;
    void *saved = NULL; size_t saved_size = 0;
    app_t app = {0}; AiContext ai = {0};
    CHECK(G_SaveGameBlob(&saved, &saved_size, "initial-states", &app, &ai, &hud));
    placed_ling->sc.flags &= ~SC_BURROWED;
    placed_ling->traits |= MF_MOBILE | MF_ATTACK;
    placed_ling->traits &= ~MF_CLOAKED;
    CHECK(G_LoadGameBlob(saved, saved_size, &app, &ai, &hud));
    free(saved);
    placed_ling = P_MobjById(ling_id);
    CHECK(placed_ling && (placed_ling->sc.flags & SC_BURROWED));
    CHECK(!(placed_ling->traits & (MF_MOBILE | MF_ATTACK)) && (placed_ling->traits & MF_CLOAKED));
    P_FreeLevel(&level); remove(map);

    P_FreeLevel(&level);
    puts("PASS: initial CHK cloak and burrow state flags");
    return 0;
}
