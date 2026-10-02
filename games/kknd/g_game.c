#include "engine.h"
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
    /* Deploys into a drill rig through the Drill Rig product (see p_prod.c). */
    { .id = MT_SURV_MOBILE_DERRICK, .name = "Mobile Derrick",
      .sprite_name = SPR(65),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 4.0f, .max_hp = 1000,
    },
    { .id = MT_SURV_OIL_TANKER, .name = "Oil Tanker",
      .sprite_name = SPR(73),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.5f, .max_hp = 750,
      .harvest = { .resources = { { .capacity = 100 } } },
    },
    { .id = MT_SURV_MOBILE_OUTPOST, .name = "Mobile Outpost",
      .sprite_name = SPR(53),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 2.5f, .max_hp = 1500,
    },
    /* === Survivor Buildings === */
    /* Oil loop: tankers load at a drill rig (the deposit) and unload at a
     * power station (the drop-off). */
    { .id = MT_SURV_DRILLRIG, .name = "Drill Rig",
      .sprite_name = SPR(75),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_SOURCE,
      .max_hp = 1000,
      .deposit = { .amount = KK_DRILLRIG_OIL, .rate = KK_DRILLRIG_RATE },
    },
    { .id = MT_SURV_POWER_STATION, .name = "Power Station",
      .sprite_name = SPR(74),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
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
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 4.0f, .max_hp = 1000,
    },
    { .id = MT_MUTE_OIL_TANKER, .name = "Mutant Oil Tanker",
      .sprite_name = SPR(48),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.5f, .max_hp = 750,
      .harvest = { .resources = { { .capacity = 100 } } },
    },
    { .id = MT_MUTE_CLANHALL_WAGON, .name = "Clanhall Wagon",
      .sprite_name = SPR(14),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = 2.5f, .max_hp = 1500,
    },
    /* === Mutant Buildings === */
    { .id = MT_MUTE_DRILLRIG, .name = "Mutant Drill Rig",
      .sprite_name = SPR(50),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_SOURCE,
      .max_hp = 1000,
      .deposit = { .amount = KK_DRILLRIG_OIL, .rate = KK_DRILLRIG_RATE },
    },
    { .id = MT_MUTE_POWER_STATION, .name = "Mutant Power Station",
      .sprite_name = SPR(49),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
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


/* The sidebar: time and money over the world, and the list of what the
 * selected building makes. */
enum { HUD_SIDEBAR, HUD_STATUS, HUD_MONEY, HUD_TECH, HUD_PRODUCTS, HUD_PAGE, NUMHUD };
static void hud_refresh(menu_t *menu);
static menuitem_t hud_items[NUMHUD] = {
    [HUD_SIDEBAR] = {.visible = true, .rect = {480, 0, 160, 480},
                     .fill = 0xff000000u, .border = 0xff686860u},
    [HUD_STATUS] = {.visible = true, .rect = {230, 0, 180, 28}, .fill = 0xff000000u,
                    .border = 0xffffffffu, .ownerdraw = HU_DrawClock},
    [HUD_MONEY] = {.visible = true, .rect = {400, 3, 0, 22}, .ink = 0xffffffffu,
                   .align = MALIGN_HCENTER, .ownerdraw = HU_DrawCounter},
    /* A line over the world: no height, so it takes no clicks. */
    [HUD_TECH] = {.visible = true, .rect = {230, 36, 400, 0}, .ink = 0xffffffffu},
    [HUD_PRODUCTS] = {.kind = MI_LIST, .visible = true, .enabled = true, .rect = {480, 32, 160, 416},
                      .row_height = 32, .value = -1, .fill = 0xff0c1216u,
                      .routine = HU_ProductList, .ownerdraw = HU_DrawProducts},
    [HUD_PAGE] = {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {480, 448, 160, 32},
                  .fill = 0xff0c1216u, .link = HUD_PRODUCTS, .routine = HU_ProductPage,
                  .ownerdraw = HU_DrawProductPage},
};
static menu_t hud = {.items = hud_items, .numitems = NUMHUD, .itemOn = -1, .refresh = hud_refresh};

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

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_kknd_map(path, out);
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    return load_assets(root, map, sprite, tileset, unit_sprite);
}

static uint16_t kknd_unit_type(const char *name) {
    for (int i = 1; i < NUMMOBJTYPES; ++i)
        if (cplc_names[i] && strcasecmp(cplc_names[i], name) == 0)
            return (uint16_t)i;
    return 0;
}

static uint16_t kknd_player_native_team(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    return strncasecmp(name, "MUTE_", 5) == 0 ? 2 : 1;
}

int P_LoadThings(const char *path) {
    KkndMapUnit native[128];
    int native_count = load_kknd_map_units(path, native,
                                           (int)(sizeof(native) / sizeof(native[0])));
    if (native_count <= 0) return 0;

    uint16_t player_team = kknd_player_native_team(path);
    fvec2_t player_position_sum = { 0.0f, 0.0f };
    int player_count = 0;
    int count = 0;
    for (int i = 0; i < native_count; ++i) {
        uint16_t type = kknd_unit_type(native[i].name);
        if (type == 0) continue;
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), type);
        if (!unit) continue;
        bool player = native[i].native_team == player_team;
        unit->core.position = fixed3_from_fvec2(native[i].position, 0);
        unit->owner = player ? 0 : 1;
        unit->team = unit->owner;
        unit->allegiance = player ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
        if (player) {
            player_position_sum = fvec2_add(player_position_sum, native[i].position);
            player_count++;
        }
        count++;
    }

    if (player_count > 0) {
        level.has_camera = true;
        level.camera = fvec2_scale(player_position_sum, 1.0f / (float)player_count);
    }
    level.player_resources[0][0] = 5000;
    level.player_resources[1][0] = 5000;
    return count;
}

bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

/* The selected building's research: its level, and the lab working on it. */
static void hud_refresh(menu_t *menu) {
    HU_RefreshProducts(menu);
    char *text = hud_items[HUD_TECH].text;
    text[0] = '\0';
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *u = hudview.units[i];
        if (u->owner != consoleplayer || !P_MobjIsSelected(u) || u->remove || u->hp <= 0) continue;
        if (!KK_NextTechLevel(u) && !u->research.level) continue;
        snprintf(text, sizeof(hud_items[HUD_TECH].text), "TECH LEVEL %d", u->research.level);
        for (int j = 0; j < hudview.unit_count; ++j) {
            const mobj_t *lab = hudview.units[j];
            if (lab->remove || lab->hp <= 0 || lab->research.target != u->id) continue;
            snprintf(text, sizeof(hud_items[HUD_TECH].text), "TECH %d - RESEARCH %d%% - %d OIL LEFT",
                u->research.level,
                100*(lab->research.total_time-lab->research.remaining_time)/lab->research.total_time,
                lab->research.remaining_cost);
            break;
        }
        break;
    }
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    (void)data_root;
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, float dt) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt);
}

irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0, 0, 480 * app->win.w / 640, app->win.h};
}
