#include "game.h"
#include "kknd.h"
#include "info.h"

#define SPR(idx) "LEVELS/640/SPRITES.LVL|" #idx ".mobd"

static const mobjtype_t ACTOR_TYPES[] = {
    /* === Survivor Infantry === */
    { .id = MT_SURV_RIFLEMAN, .name = "Rifleman",
      .sprite_name = SPR(34),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 100,
      .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 650 },
    },
    { .id = MT_SURV_FLAMER, .name = "Flamer",
      .sprite_name = SPR(25),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 100,
      .attack = { .range = 3.0f, .damage = 4, .cooldown_ms = 1300 },
    },
    { .id = MT_SURV_SWAT, .name = "SWAT",
      .sprite_name = SPR(76),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 125,
      .attack = { .range = 4.5f, .damage = 17, .cooldown_ms = 660 },
    },
    { .id = MT_SURV_SAPPER, .name = "Sapper",
      .sprite_name = SPR(63),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 125,
      .attack = { .range = 4.0f, .damage = 22, .cooldown_ms = 1300 },
    },
    { .id = MT_SURV_SABOTEUR, .name = "Saboteur",
      .sprite_name = SPR(62),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.5f, .max_hp = 150,
      .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 1300 },
    },
    { .id = MT_SURV_TECHNICIAN, .name = "Technician",
      .sprite_name = SPR(78),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 4.5f, .max_hp = 125,
    },
    { .id = MT_SURV_RPG_LAUNCHER, .name = "RPG Launcher",
      .sprite_name = SPR(59),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 100,
      .attack = { .range = 6.0f, .damage = 20, .cooldown_ms = 1650 },
    },
    { .id = MT_SURV_SNIPER, .name = "Sniper",
      .sprite_name = SPR(71),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 150,
      .attack = { .range = 9.0f, .damage = 62, .cooldown_ms = 990 },
    },
    /* === Survivor Vehicles === */
    { .id = MT_SURV_DIRT_BIKE, .name = "Dirt Bike",
      .sprite_name = SPR(7),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 9.0f, .max_hp = 125,
      .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 660 },
    },
    { .id = MT_SURV_4X4_PICKUP, .name = "4x4 Pickup",
      .sprite_name = SPR(54),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 7.5f, .max_hp = 200,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 495 },
    },
    { .id = MT_SURV_ATV, .name = "All-Terrain Vehicle",
      .sprite_name = SPR(1),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 7.0f, .max_hp = 300,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 330 },
    },
    { .id = MT_SURV_ATV_FLAMETHROWER, .name = "Flame ATV",
      .sprite_name = SPR(24),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 6.5f, .max_hp = 300,
      .attack = { .range = 3.0f, .damage = 4, .cooldown_ms = 1300 },
    },
    { .id = MT_SURV_ANACONDA_TANK, .name = "Anaconda Tank",
      .sprite_name = SPR(77),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 5.5f, .max_hp = 400,
      .attack = { .range = 7.0f, .damage = 25, .cooldown_ms = 1155 },
    },
    { .id = MT_SURV_BARRAGE_CRAFT, .name = "Barrage Craft",
      .sprite_name = SPR(2),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 450,
      .attack = { .range = 8.0f, .damage = 37, .cooldown_ms = 220 },
    },
    { .id = MT_SURV_AUTOCANNON_TANK, .name = "Autocannon Tank",
      .sprite_name = SPR(11),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 425,
      .attack = { .range = 6.0f, .damage = 10, .cooldown_ms = 55 },
    },
    /* === Survivor Harvesters === */
    { .id = MT_SURV_MOBILE_DERRICK, .name = "Mobile Derrick",
      .sprite_name = SPR(65),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.0f, .max_hp = 1000,
      .harvest = { .capacity = 150 },
    },
    { .id = MT_SURV_OIL_TANKER, .name = "Oil Tanker",
      .sprite_name = SPR(73),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.5f, .max_hp = 750,
      .harvest = { .capacity = 100 },
    },
    { .id = MT_SURV_MOBILE_OUTPOST, .name = "Mobile Outpost",
      .sprite_name = SPR(53),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 2.5f, .max_hp = 1500,
    },
    /* === Survivor Buildings === */
    { .id = MT_SURV_DRILLRIG, .name = "Drill Rig",
      .sprite_name = SPR(75),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1000,
    },
    { .id = MT_SURV_POWER_STATION, .name = "Power Station",
      .sprite_name = SPR(74),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 1000,
    },
    { .id = MT_SURV_OUTPOST, .name = "Outpost",
      .sprite_name = SPR(52),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 1500,
    },
    { .id = MT_SURV_MACHINE_SHOP, .name = "Machine Shop",
      .sprite_name = SPR(37),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 1000,
    },
    { .id = MT_SURV_REPAIR_BAY, .name = "Repair Bay",
      .sprite_name = SPR(56),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 750,
    },
    { .id = MT_SURV_RESEARCH_LAB, .name = "Research Lab",
      .sprite_name = SPR(57),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 750,
    },
    /* === Survivor Towers === */
    { .id = MT_SURV_GUARD_TOWER, .name = "Guard Tower",
      .sprite_name = SPR(67),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 300,
      .attack = { .range = 6.0f, .damage = 10, .cooldown_ms = 165 },
    },
    { .id = MT_SURV_MISSILE_BATTERY, .name = "Missile Battery",
      .sprite_name = SPR(44),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 450,
      .attack = { .range = 9.0f, .damage = 37, .cooldown_ms = 330 },
    },
    { .id = MT_SURV_CANNON_TOWER, .name = "Cannon Tower",
      .sprite_name = SPR(12),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 600,
      .attack = { .range = 8.0f, .damage = 20, .cooldown_ms = 110 },
    },
    /* === Survivor Aircraft === */
    { .id = MT_SURV_BOMBER, .name = "Bomber",
      .sprite_name = SPR(83),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY | MF_ATTACK,
      .speed = 14.0f, .max_hp = 375,
      .attack = { .range = 3.0f, .damage = 2000, .cooldown_ms = 3000 },
    },
    /* === Mutant Infantry === */
    { .id = MT_MUTE_BERSERKER, .name = "Berserker",
      .sprite_name = SPR(5),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 80,
      .attack = { .range = 2.5f, .damage = 10, .cooldown_ms = 660 },
    },
    { .id = MT_MUTE_PYROMANIAC, .name = "Pyromaniac",
      .sprite_name = SPR(55),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 100,
      .attack = { .range = 3.0f, .damage = 4, .cooldown_ms = 1300 },
    },
    { .id = MT_MUTE_SHOTGUNNER, .name = "Shotgunner",
      .sprite_name = SPR(68),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 125,
      .attack = { .range = 4.5f, .damage = 17, .cooldown_ms = 990 },
    },
    { .id = MT_MUTE_RIOTER, .name = "Rioter",
      .sprite_name = SPR(58),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 125,
      .attack = { .range = 4.0f, .damage = 22, .cooldown_ms = 1300 },
    },
    { .id = MT_MUTE_VANDAL, .name = "Vandal",
      .sprite_name = SPR(81),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.5f, .max_hp = 150,
      .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 1300 },
    },
    { .id = MT_MUTE_MEKANIK, .name = "Mekanik",
      .sprite_name = SPR(41),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 4.5f, .max_hp = 125,
    },
    { .id = MT_MUTE_BAZOOKA, .name = "Bazooka",
      .sprite_name = SPR(60),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 100,
      .attack = { .range = 6.0f, .damage = 20, .cooldown_ms = 1650 },
    },
    { .id = MT_MUTE_CRAZY_HARRY, .name = "Crazy Harry",
      .sprite_name = SPR(31),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 125,
      .attack = { .range = 5.0f, .damage = 62, .cooldown_ms = 330 },
    },
    /* === Mutant Vehicles === */
    { .id = MT_MUTE_DIRE_WOLF, .name = "Dire Wolf",
      .sprite_name = SPR(19),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 8.5f, .max_hp = 150,
      .attack = { .range = 2.5f, .damage = 10, .cooldown_ms = 660 },
    },
    { .id = MT_MUTE_BIKE_SIDECAR, .name = "Bike and Sidecar",
      .sprite_name = SPR(70),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 8.0f, .max_hp = 175,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 495 },
    },
    { .id = MT_MUTE_MONSTER_TRUCK, .name = "Monster Truck",
      .sprite_name = SPR(47),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 6.5f, .max_hp = 250,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 330 },
    },
    { .id = MT_MUTE_GIANT_SCORPION, .name = "Giant Scorpion",
      .sprite_name = SPR(64),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 5.5f, .max_hp = 250,
      .attack = { .range = 5.0f, .damage = 12, .cooldown_ms = 1300 },
    },
    { .id = MT_MUTE_WAR_MASTADONT, .name = "War Mastodon",
      .sprite_name = SPR(38),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.5f, .max_hp = 400,
      .attack = { .range = 7.0f, .damage = 6, .cooldown_ms = 110 },
    },
    { .id = MT_MUTE_GIANT_BEETLE, .name = "Giant Beetle",
      .sprite_name = SPR(4),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 300,
      .attack = { .range = 6.0f, .damage = 12, .cooldown_ms = 1650 },
    },
    { .id = MT_MUTE_MISSILE_CRAB, .name = "Missile Crab",
      .sprite_name = SPR(16),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 450,
      .attack = { .range = 8.0f, .damage = 100, .cooldown_ms = 990 },
    },
    /* === Mutant Harvesters === */
    { .id = MT_MUTE_MOBILE_DERRICK, .name = "Mutant Mobile Derrick",
      .sprite_name = SPR(39),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.0f, .max_hp = 1000,
      .harvest = { .capacity = 150 },
    },
    { .id = MT_MUTE_OIL_TANKER, .name = "Mutant Oil Tanker",
      .sprite_name = SPR(48),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.5f, .max_hp = 750,
      .harvest = { .capacity = 100 },
    },
    { .id = MT_MUTE_CLANHALL_WAGON, .name = "Clanhall Wagon",
      .sprite_name = SPR(14),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 2.5f, .max_hp = 1500,
    },
    /* === Mutant Buildings === */
    { .id = MT_MUTE_DRILLRIG, .name = "Mutant Drill Rig",
      .sprite_name = SPR(50),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1000,
    },
    { .id = MT_MUTE_POWER_STATION, .name = "Mutant Power Station",
      .sprite_name = SPR(49),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 1000,
    },
    { .id = MT_MUTE_CLANHALL, .name = "Clan Hall",
      .sprite_name = SPR(13),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 1500,
    },
    { .id = MT_MUTE_BLACKSMITH, .name = "Blacksmith",
      .sprite_name = SPR(8),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 800,
    },
    { .id = MT_MUTE_BEAST_ENCLOSURE, .name = "Beast Enclosure",
      .sprite_name = SPR(3),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 800,
    },
    { .id = MT_MUTE_MENAGERIE, .name = "Menagerie",
      .sprite_name = SPR(42),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 750,
    },
    { .id = MT_MUTE_ALCHEMY_HALL, .name = "Alchemy Hall",
      .sprite_name = SPR(0),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 750,
    },
    /* === Mutant Towers === */
    { .id = MT_MUTE_MACHINEGUN_NEST, .name = "Machinegun Nest",
      .sprite_name = SPR(43),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 300,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 165 },
    },
    { .id = MT_MUTE_GRAPESHOT_TOWER, .name = "Grapeshot Tower",
      .sprite_name = SPR(29),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 450,
      .attack = { .range = 8.0f, .damage = 5, .cooldown_ms = 0 },
    },
    { .id = MT_MUTE_ROTARY_CANNON, .name = "Rotary Cannon",
      .sprite_name = SPR(61),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 625,
      .attack = { .range = 7.0f, .damage = 10, .cooldown_ms = 66 },
    },
    /* === Mutant Aircraft === */
    { .id = MT_MUTE_WASP, .name = "Wasp",
      .sprite_name = SPR(82),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY | MF_ATTACK,
      .speed = 14.0f, .max_hp = 375,
      .attack = { .range = 3.0f, .damage = 2000, .cooldown_ms = 3000 },
    },
};

static const uidefinition_t UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 0, 480, 480 },
    .command_grid = { 480, 32, 160, 448 },
    .resources = {
        [0] = { .text = { 400, 3 }, .color = { 255, 255, 255, 255 } },
    },
    .resource_count = 1,
    .status_panel = {
        .rect = { 230, 0, 180, 28 },
        .fill = { 0, 0, 0, 255 },
        .border = { 255, 255, 255, 255 },
    },
    .status_elapsed_time = true,
    .sidebar_panel = {
        .rect = { 480, 0, 160, 480 },
        .fill = { 0, 0, 0, 255 },
        .border = { 104, 104, 96, 255 },
    },
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "kknd";
const char *const g_game_name          = "KKnD";
const char *const g_game_default_root  = "data/KKND";
const char *const g_game_default_map   = "LEVELS/640/SURV_01.LVL";
const char *const g_game_default_sprite = "LEVELS/640/SPRITES.LVL|Infantry.mobd";
const int g_cell_w = 32;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = MT_MUTE_BERSERKER;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types = ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));
const uidefinition_t *const gameui = &UI;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_kknd_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    (void)path;
    int count = 0;
    float cx = level.width * 0.25f;
    float cy = level.height * 0.25f;

    /* Spawn player base + starting units */
    struct { uint16_t type; float dx; float dy; } player_units[] = {
        { MT_SURV_DRILLRIG,  0.0f,  0.0f },
        { MT_SURV_OIL_TANKER, -2.0f, 0.0f },
        { MT_SURV_RIFLEMAN,  2.0f, -1.0f },
        { MT_SURV_RIFLEMAN,  2.0f,  1.0f },
        { MT_SURV_RIFLEMAN,  3.0f,  0.0f },
        { MT_SURV_OUTPOST,  -3.0f, -2.0f },
    };
    for (int i = 0; i < (int)(sizeof(player_units) / sizeof(player_units[0])); ++i) {
        fvec2_t pos = { cx + player_units[i].dx, cy + player_units[i].dy };
        if (pos.x < 1) pos.x = 1;
        if (pos.y < 1) pos.y = 1;
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), player_units[i].type);
        if (!unit) continue;
        unit->core.position = fixed3_from_fvec2(pos, 0);
        unit->owner = 0;
        unit->team = 0;
        unit->type_id = player_units[i].type;
        count++;
    }

    /* Spawn enemy base + units on the far side */
    float ex = level.width * 0.75f;
    float ey = level.height * 0.75f;
    struct { uint16_t type; float dx; float dy; } enemy_units[] = {
        { MT_MUTE_DRILLRIG,   0.0f,  0.0f },
        { MT_MUTE_OIL_TANKER, 2.0f,  0.0f },
        { MT_MUTE_BERSERKER, -2.0f, -1.0f },
        { MT_MUTE_BERSERKER, -2.0f,  1.0f },
        { MT_MUTE_SHOTGUNNER,-3.0f,  0.0f },
        { MT_MUTE_CLANHALL,   3.0f, -2.0f },
    };
    for (int i = 0; i < (int)(sizeof(enemy_units) / sizeof(enemy_units[0])); ++i) {
        fvec2_t pos = { ex + enemy_units[i].dx, ey + enemy_units[i].dy };
        if (pos.x >= level.width) pos.x = (float)level.width - 1.0f;
        if (pos.y >= level.height) pos.y = (float)level.height - 1.0f;
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), enemy_units[i].type);
        if (!unit) continue;
        unit->core.position = fixed3_from_fvec2(pos, 0);
        unit->owner = 1;
        unit->team = 1;
        unit->allegiance = ALLEGIANCE_ENEMY;
        unit->type_id = enemy_units[i].type;
        count++;
    }

    /* Place resource vents near both bases */
    static const struct { float fx; float fy; } vent_rel[] = {
        { -6.0f, -4.0f }, { 6.0f, 4.0f },
        { -4.0f, 6.0f },  { 4.0f, -6.0f },
    };
    float bases[][2] = { { cx, cy }, { ex, ey } };
    for (int b = 0; b < 2; ++b) {
        for (int v = 0; v < (int)(sizeof(vent_rel) / sizeof(vent_rel[0])); ++v) {
            float vx = bases[b][0] + vent_rel[v].fx;
            float vy = bases[b][1] + vent_rel[v].fy;
            int ix = (int)vx, iy = (int)vy;
            if (!L_Contains(&level, ix, iy)) continue;
            resourcevent_t *vents = realloc(level.resource_vents,
                (size_t)(level.resource_vent_count + 1) * sizeof(resourcevent_t));
            if (!vents) break;
            level.resource_vents = vents;
            resourcevent_t *rv = &level.resource_vents[level.resource_vent_count++];
            rv->cell = (ivec2_t){ ix, iy };
            rv->attachment = (fvec2_t){ vx + 0.5f, vy + 0.5f };
            rv->amount = 5000;
            rv->rate = 25;
            rv->active = true;
            rv->resource_type = 0;
        }
    }

    level.player_resources[0][0] = 5000;
    level.player_resources[1][0] = 5000;
    level.has_camera = true;
    level.camera = (fvec2_t){ cx, cy };
    return count;
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    (void)app; (void)data_root;
    return NULL;
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *const *units, int unit_count, const SDL_Event *event) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count; (void)event;
    return false;
}

void G_CustomUITicker(void *ui) {
    (void)ui;
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count;
    (void)sprites; (void)hud;
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                        float dt) {
    (void)ui; (void)map; (void)units; (void)unit_count;
    (void)dt;
    return false;
}

void G_ShutdownCustomUI(void *ui) {
    (void)ui;
}

int G_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    if (gameui && gameui->world_viewport.w > 0 && gameui->logical_width > 0)
        return gameui->world_viewport.w * app->win.w / gameui->logical_width;
    return app->win.w > 0 ? app->win.w : 1;
}
