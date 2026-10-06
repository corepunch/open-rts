#include "engine.h"
#include "kknd.h"
#include "info.h"

#define SPR(idx) "LEVELS/640/SPRITES.LVL|" #idx ".mobd"

/* UNITS.CFG: speed (px/s) and range (px) over 32-pixel cells, reloads in
   1/60 s, tspeed in quarter turns per second, damage by victim class. */
#define KK_CELLS(px) ((px) / 32.0f)
#define KK_MS(sixtieths) ((sixtieths) * 1000 / 60)
#define KK_TURN(tspeed) (ANG90 / RTS_TICRATE * (tspeed))
#define KK_ATTACK(px, reload, i, v, b) \
    { .range = KK_CELLS(px), .damage = (i), .versus = { (i), (v), (b) }, \
      .cooldown_ms = KK_MS(reload) }
/* Turrets fire volleys: reload between shots, reload2 after each volley. */
#define KK_VOLLEY(px, reload, reload2, volley, i, v, b) \
    { .range = KK_CELLS(px), .damage = (i), .versus = { (i), (v), (b) }, \
      .cooldown_ms = KK_MS(reload), .shots = (volley), .reload_ms = KK_MS(reload2) }
enum { KK_ARMOR_INFANTRY, KK_ARMOR_VEHICLE, KK_ARMOR_STRUCTURE };

static const mobjtype_t ACTOR_TYPES[] = {
    /* === Survivor Infantry === */
    { .id = MT_SURV_RIFLEMAN, .name = "Rifleman",
      .sprite_name = SPR(34),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 400,
      .attack = KK_ATTACK(96, 60, 40, 30, 15),
    },
    { .id = MT_SURV_FLAMER, .name = "Flamer",
      .sprite_name = SPR(25),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 400,
      .attack = KK_ATTACK(80, 120, 15, 15, 18),
    },
    { .id = MT_SURV_SWAT, .name = "SWAT",
      .sprite_name = SPR(76),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 500,
      .attack = KK_ATTACK(128, 60, 70, 55, 30),
    },
    { .id = MT_SURV_SAPPER, .name = "Sapper",
      .sprite_name = SPR(63),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 500,
      .attack = KK_ATTACK(96, 120, 90, 150, 120),
    },
    { .id = MT_SURV_SABOTEUR, .name = "Saboteur",
      .sprite_name = SPR(62),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(64), .max_hp = 600,
      .attack = KK_ATTACK(96, 120, 40, 30, 15),
    },
    { .id = MT_SURV_TECHNICIAN, .name = "Technician",
      .sprite_name = SPR(78),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(64), .max_hp = 500,
    },
    { .id = MT_SURV_RPG_LAUNCHER, .name = "RPG Launcher",
      .sprite_name = SPR(59),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 400,
      .attack = KK_ATTACK(160, 150, 80, 130, 90),
    },
    { .id = MT_SURV_SNIPER, .name = "Sniper",
      .sprite_name = SPR(71),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(64), .max_hp = 600,
      .attack = KK_ATTACK(224, 90, 250, 90, 50),
    },
    /* === Survivor Vehicles === */
    { .id = MT_SURV_DIRT_BIKE, .name = "Dirt Bike",
      .sprite_name = SPR(7),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(80), .turn_step = KK_TURN(3), .max_hp = 500,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_ATTACK(128, 60, 40, 30, 15),
    },
    { .id = MT_SURV_4X4_PICKUP, .name = "4x4 Pickup",
      .sprite_name = SPR(54),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(70), .turn_step = KK_TURN(3), .max_hp = 800,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(128, 45, 60, 10, 40, 30, 15),
    },
    { .id = MT_SURV_ATV, .name = "All-Terrain Vehicle",
      .sprite_name = SPR(1),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(60), .turn_step = KK_TURN(3), .max_hp = 1200,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(160, 30, 90, 10, 40, 30, 15),
    },
    { .id = MT_SURV_ATV_FLAMETHROWER, .name = "Flame ATV",
      .sprite_name = SPR(24),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(55), .turn_step = KK_TURN(3), .max_hp = 1200,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(96, 120, 120, 1, 15, 15, 18),
    },
    { .id = MT_SURV_ANACONDA_TANK, .name = "Anaconda Tank",
      .sprite_name = SPR(77),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(45), .turn_step = KK_TURN(3), .max_hp = 1600,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(192, 105, 105, 1, 100, 200, 100),
    },
    { .id = MT_SURV_BARRAGE_CRAFT, .name = "Barrage Craft",
      .sprite_name = SPR(2),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 1800,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(240, 20, 420, 6, 150, 170, 120),
    },
    { .id = MT_SURV_AUTOCANNON_TANK, .name = "Autocannon Tank",
      .sprite_name = SPR(11),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 1700,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(224, 5, 5, 1, 40, 30, 15),
    },
    /* === Survivor Harvesters === */
    /* Deploys into a drill rig through the Drill Rig product (see p_prod.c). */
    { .id = MT_SURV_MOBILE_DERRICK, .name = "Mobile Derrick",
      .sprite_name = SPR(65),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 4000,
      .armor_class = KK_ARMOR_VEHICLE,
    },
    { .id = MT_SURV_OIL_TANKER, .name = "Oil Tanker",
      .sprite_name = SPR(73),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(3), .max_hp = 3000,
      .armor_class = KK_ARMOR_VEHICLE,
      .harvest = { .resources = { { .capacity = 100 } } },
    },
    { .id = MT_SURV_MOBILE_OUTPOST, .name = "Mobile Outpost",
      .sprite_name = SPR(53),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(20), .turn_step = KK_TURN(2), .max_hp = 6000,
      .armor_class = KK_ARMOR_VEHICLE,
    },
    /* === Survivor Buildings === */
    /* Oil loop: tankers load at a drill rig (the deposit) and unload at a
     * power station (the drop-off). */
    { .id = MT_SURV_DRILLRIG, .name = "Drill Rig",
      .sprite_name = SPR(75),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_SOURCE,
      .max_hp = 4000, .armor_class = KK_ARMOR_STRUCTURE,
      .deposit = { .amount = KK_DRILLRIG_OIL, .rate = KK_DRILLRIG_RATE },
    },
    { .id = MT_SURV_POWER_STATION, .name = "Power Station",
      .sprite_name = SPR(74),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 4000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_SURV_OUTPOST, .name = "Outpost",
      .sprite_name = SPR(52),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 6000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_SURV_MACHINE_SHOP, .name = "Machine Shop",
      .sprite_name = SPR(37),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 4000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_SURV_REPAIR_BAY, .name = "Repair Bay",
      .sprite_name = SPR(56),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_SURV_RESEARCH_LAB, .name = "Research Lab",
      .sprite_name = SPR(57),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    /* === Survivor Towers === */
    { .id = MT_SURV_GUARD_TOWER, .name = "Guard Tower",
      .sprite_name = SPR(67),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 1200, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(192, 15, 90, 10, 40, 30, 15),
    },
    { .id = MT_SURV_MISSILE_BATTERY, .name = "Missile Battery",
      .sprite_name = SPR(44),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 1800, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(256, 30, 120, 3, 150, 150, 100),
    },
    { .id = MT_SURV_CANNON_TOWER, .name = "Cannon Tower",
      .sprite_name = SPR(12),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 2400, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(256, 10, 125, 2, 80, 150, 80),
    },
    /* === Survivor Aircraft === */
    { .id = MT_SURV_BOMBER, .name = "Bomber",
      .sprite_name = SPR(83),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY | MF_ATTACK,
      .speed = KK_CELLS(120), .turn_step = KK_TURN(1), .max_hp = 1500,
      .armor_class = KK_ARMOR_VEHICLE,
      /* UNITS.CFG has no bomber range or reload; these remain engine values. */
      .attack = { .range = 3.0f, .damage = 2000, .versus = { 2000, 2900, 1500 },
                  .cooldown_ms = 3000 },
    },
    /* === Mutant Infantry === */
    { .id = MT_MUTE_BERSERKER, .name = "Berserker",
      .sprite_name = SPR(5),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 320,
      .attack = KK_ATTACK(96, 60, 40, 30, 15),
    },
    { .id = MT_MUTE_PYROMANIAC, .name = "Pyromaniac",
      .sprite_name = SPR(55),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 400,
      .attack = KK_ATTACK(80, 120, 15, 15, 18),
    },
    { .id = MT_MUTE_SHOTGUNNER, .name = "Shotgunner",
      .sprite_name = SPR(68),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 500,
      .attack = KK_ATTACK(128, 90, 70, 55, 30),
    },
    { .id = MT_MUTE_RIOTER, .name = "Rioter",
      .sprite_name = SPR(58),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 500,
      .attack = KK_ATTACK(96, 120, 90, 150, 120),
    },
    { .id = MT_MUTE_VANDAL, .name = "Vandal",
      .sprite_name = SPR(81),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(64), .max_hp = 600,
      .attack = KK_ATTACK(96, 120, 40, 30, 15),
    },
    { .id = MT_MUTE_MEKANIK, .name = "Mekanik",
      .sprite_name = SPR(41),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(64), .max_hp = 500,
    },
    { .id = MT_MUTE_BAZOOKA, .name = "Bazooka",
      .sprite_name = SPR(60),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 400,
      .attack = KK_ATTACK(160, 150, 80, 130, 90),
    },
    { .id = MT_MUTE_CRAZY_HARRY, .name = "Crazy Harry",
      .sprite_name = SPR(31),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(64), .max_hp = 500,
      .attack = KK_ATTACK(192, 30, 250, 90, 50),
    },
    /* === Mutant Vehicles === */
    { .id = MT_MUTE_DIRE_WOLF, .name = "Dire Wolf",
      .sprite_name = SPR(19),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(75), .turn_step = KK_TURN(16), .max_hp = 600,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_ATTACK(128, 60, 40, 30, 15),
    },
    { .id = MT_MUTE_BIKE_SIDECAR, .name = "Bike and Sidecar",
      .sprite_name = SPR(70),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(70), .turn_step = KK_TURN(3), .max_hp = 700,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(128, 45, 60, 10, 40, 30, 15),
    },
    { .id = MT_MUTE_MONSTER_TRUCK, .name = "Monster Truck",
      .sprite_name = SPR(47),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(55), .turn_step = KK_TURN(3), .max_hp = 1000,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(160, 30, 90, 10, 40, 30, 15),
    },
    { .id = MT_MUTE_GIANT_SCORPION, .name = "Giant Scorpion",
      .sprite_name = SPR(64),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(45), .turn_step = KK_TURN(3), .max_hp = 1000,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_ATTACK(160, 120, 50, 100, 70),
    },
    { .id = MT_MUTE_WAR_MASTADONT, .name = "War Mastodon",
      .sprite_name = SPR(38),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(3), .max_hp = 1600,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(192, 10, 160, 4, 25, 50, 25),
    },
    { .id = MT_MUTE_GIANT_BEETLE, .name = "Giant Beetle",
      .sprite_name = SPR(4),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 1200,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_ATTACK(192, 150, 50, 60, 40),
    },
    { .id = MT_MUTE_MISSILE_CRAB, .name = "Missile Crab",
      .sprite_name = SPR(16),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 1800,
      .armor_class = KK_ARMOR_VEHICLE,
      .attack = KK_VOLLEY(256, 30, 90, 2, 100, 180, 120),
    },
    /* === Mutant Harvesters === */
    { .id = MT_MUTE_MOBILE_DERRICK, .name = "Mutant Mobile Derrick",
      .sprite_name = SPR(39),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(30), .turn_step = KK_TURN(3), .max_hp = 4000,
      .armor_class = KK_ARMOR_VEHICLE,
    },
    { .id = MT_MUTE_OIL_TANKER, .name = "Mutant Oil Tanker",
      .sprite_name = SPR(48),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = KK_CELLS(35), .turn_step = KK_TURN(3), .max_hp = 3000,
      .armor_class = KK_ARMOR_VEHICLE,
      .harvest = { .resources = { { .capacity = 100 } } },
    },
    { .id = MT_MUTE_CLANHALL_WAGON, .name = "Clanhall Wagon",
      .sprite_name = SPR(14),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = KK_CELLS(20), .turn_step = KK_TURN(2), .max_hp = 6000,
      .armor_class = KK_ARMOR_VEHICLE,
    },
    /* === Mutant Buildings === */
    { .id = MT_MUTE_DRILLRIG, .name = "Mutant Drill Rig",
      .sprite_name = SPR(50),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_SOURCE,
      .max_hp = 4000, .armor_class = KK_ARMOR_STRUCTURE,
      .deposit = { .amount = KK_DRILLRIG_OIL, .rate = KK_DRILLRIG_RATE },
    },
    { .id = MT_MUTE_POWER_STATION, .name = "Mutant Power Station",
      .sprite_name = SPR(49),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 4000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_MUTE_CLANHALL, .name = "Clan Hall",
      .sprite_name = SPR(13),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 6000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_MUTE_BLACKSMITH, .name = "Blacksmith",
      .sprite_name = SPR(8),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3200, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_MUTE_BEAST_ENCLOSURE, .name = "Beast Enclosure",
      .sprite_name = SPR(3),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3200, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_MUTE_MENAGERIE, .name = "Menagerie",
      .sprite_name = SPR(42),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    { .id = MT_MUTE_ALCHEMY_HALL, .name = "Alchemy Hall",
      .sprite_name = SPR(0),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 3000, .armor_class = KK_ARMOR_STRUCTURE,
    },
    /* === Mutant Towers === */
    { .id = MT_MUTE_MACHINEGUN_NEST, .name = "Machinegun Nest",
      .sprite_name = SPR(43),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 1200, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(192, 15, 90, 10, 40, 30, 15),
    },
    { .id = MT_MUTE_GRAPESHOT_TOWER, .name = "Grapeshot Tower",
      .sprite_name = SPR(29),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 1800, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(256, 0, 120, 10, 20, 17, 12),
    },
    { .id = MT_MUTE_ROTARY_CANNON, .name = "Rotary Cannon",
      .sprite_name = SPR(61),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 2500, .armor_class = KK_ARMOR_STRUCTURE,
      .attack = KK_VOLLEY(260, 6, 10, 10, 40, 30, 15),
    },
    /* === Mutant Aircraft === */
    { .id = MT_MUTE_WASP, .name = "Wasp",
      .sprite_name = SPR(82),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY | MF_ATTACK,
      .speed = KK_CELLS(120), .turn_step = KK_TURN(1), .max_hp = 1500,
      .armor_class = KK_ARMOR_VEHICLE,
      /* UNITS.CFG has no wasp range or reload; these remain engine values. */
      .attack = { .range = 3.0f, .damage = 2000, .versus = { 2000, 2900, 1500 },
                  .cooldown_ms = 3000 },
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
    hudview = (hudview_t){0};
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, float dt) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt);
}

irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0, 0, 480 * app->win.w / 640, app->win.h};
}
