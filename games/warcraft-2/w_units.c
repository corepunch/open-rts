#include "w2_local.h"

#include <string.h>

/* Stats are the Wargus unit table, authored here as the gameplay source.
 * GRP indices are the MAINDAT entry numbers published by war2tools (MIT).
 * A hero with no entry of its own reuses the line unit's forest GRP; that
 * share is inferred from Wargus animations, not from a traced WAR2.EXE path.
 * Fire breeze has no confirmed entry. Daemon is stored as land: Wargus marks
 * it fly, but the figure walks and DEATH places it on land. */
#define LAND (W2_MOBILE | W2_COMBAT)
#define HARV (W2_MOBILE | W2_HARVEST | W2_COMBAT)
#define SHIP (W2_MOBILE | W2_SEA | W2_COMBAT)
#define TANK (W2_MOBILE | W2_SEA)
#define FLY  (W2_MOBILE | W2_AIR | W2_COMBAT)
#define BALLOON (W2_MOBILE | W2_AIR)
#define BLD  W2_STRUCTURE
#define HALL (W2_STRUCTURE | W2_HALL)
#define CRIT (W2_MOBILE | W2_CRITTER)

const w2_unit_t w2_units[W2_TYPE_COUNT] = {
    /* 0 */  { "footman", LAND, 1, 1, 60, 10, 9, 1, 4, { 45, 0, 0, 0 }, 2, 2, NULL },
    /* 1 */  { "grunt", LAND, 1, 1, 60, 10, 9, 1, 4, { 46, 0, 0, 0 }, 2, 2, NULL },
    /* 2 */  { "peasant", HARV, 1, 1, 30, 10, 5, 1, 4, { 47, 0, 0, 0 }, 0, 1, NULL },
    /* 3 */  { "peon", HARV, 1, 1, 30, 10, 5, 1, 4, { 48, 0, 0, 0 }, 0, 1, NULL },
    /* 4 */  { "ballista", LAND, 1, 1, 110, 5, 80, 8, 9, { 49, 0, 0, 0 }, 0, 25, NULL },
    /* 5 */  { "catapult", LAND, 1, 1, 110, 5, 80, 8, 9, { 50, 0, 0, 0 }, 0, 25, NULL },
    /* 6 */  { "knight", LAND, 1, 1, 90, 13, 12, 1, 4, { 51, 0, 0, 0 }, 4, 2, NULL },
    /* 7 */  { "ogre", LAND, 1, 1, 90, 13, 12, 1, 4, { 52, 0, 0, 0 }, 4, 2, NULL },
    /* 8 */  { "archer", LAND, 1, 1, 40, 10, 9, 4, 5, { 53, 0, 0, 0 }, 0, 3, NULL },
    /* 9 */  { "axethrower", LAND, 1, 1, 40, 10, 9, 4, 5, { 54, 0, 0, 0 }, 0, 3, NULL },
    /* 10 */ { "mage", LAND, 1, 1, 60, 8, 9, 2, 9, { 55, 0, 0, 0 }, 0, 5, NULL },
    /* 11 */ { "death-knight", LAND, 1, 1, 60, 8, 9, 3, 9, { 58, 0, 0, 0 }, 0, 5, NULL },
    /* 12 */ { "paladin", LAND, 1, 1, 90, 13, 12, 1, 5, { 51, 0, 0, 0 }, 4, 2, NULL },
    /* 13 */ { "ogre-mage", LAND, 1, 1, 90, 13, 12, 1, 5, { 52, 0, 0, 0 }, 4, 2, NULL },
    /* 14 */ { "dwarves", LAND, 1, 1, 40, 11, 6, 1, 4, { 33, 0, 0, 0 }, 0, 1, "Demolition Squad" },
    /* 15 */ { "goblin-sappers", LAND, 1, 1, 40, 11, 6, 1, 4, { 34, 0, 0, 0 }, 0, 1, NULL },
    /* 16 */ { "attack-peasant", LAND, 1, 1, 30, 10, 5, 1, 4, { 47, 0, 0, 0 }, 0, 1, "Peasant" },
    /* 17 */ { "attack-peon", HARV, 1, 1, 30, 10, 5, 1, 4, { 48, 0, 0, 0 }, 0, 1, "Peon" },
    /* 18 */ { "ranger", LAND, 1, 1, 50, 10, 9, 4, 6, { 53, 0, 0, 0 }, 0, 3, NULL },
    /* 19 */ { "berserker", LAND, 1, 1, 50, 10, 9, 4, 6, { 54, 0, 0, 0 }, 0, 3, NULL },
    /* 20 */ { "female-hero", LAND, 1, 1, 120, 10, 28, 7, 9, { 53, 0, 0, 0 }, 5, 9, "Alleria" },
    /* 21 */ { "evil-knight", LAND, 1, 1, 180, 8, 16, 4, 9, { 58, 0, 0, 0 }, 2, 8, "Teron Gorefiend" },
    /* 22 */ { "flying-angel", FLY, 2, 2, 250, 14, 25, 5, 9, { 35, 0, 0, 0 }, 6, 13, "Kurdran and Sky'ree" },
    /* 23 */ { "fad-man", LAND, 1, 1, 300, 13, 24, 1, 6, { 52, 0, 0, 0 }, 8, 3, "Dentarg" },
    /* 24 */ { "white-mage", LAND, 1, 1, 120, 8, 16, 6, 9, { 55, 0, 0, 0 }, 3, 8, "Khadgar" },
    /* 25 */ { "beast-cry", LAND, 1, 1, 240, 10, 22, 1, 5, { 54, 0, 0, 0 }, 8, 3, "Grom Hellscream" },
    /* 26 */ { "human-tanker", TANK, 2, 2, 90, 10, 0, 1, 4, { 59, 0, 0, 0 }, 10, 0, "Oil Tanker" },
    /* 27 */ { "orc-tanker", TANK, 2, 2, 90, 10, 0, 1, 4, { 60, 0, 0, 0 }, 10, 0, "Oil Tanker" },
    /* 28 */ { "human-transport", TANK, 2, 2, 150, 10, 0, 1, 4, { 39, 0, 0, 0 }, 0, 0, "Transport" },
    /* 29 */ { "orc-transport", TANK, 2, 2, 150, 10, 0, 1, 4, { 40, 0, 0, 0 }, 0, 0, "Transport" },
    /* 30 */ { "human-destroyer", SHIP, 2, 2, 100, 10, 35, 4, 8, { 61, 0, 0, 0 }, 10, 2, "Elven Destroyer" },
    /* 31 */ { "orc-destroyer", SHIP, 2, 2, 100, 10, 35, 4, 8, { 62, 0, 0, 0 }, 10, 2, "Troll Destroyer" },
    /* 32 */ { "battleship", SHIP, 2, 2, 150, 6, 130, 6, 8, { 41, 0, 0, 0 }, 15, 50, NULL },
    /* 33 */ { "juggernaught", SHIP, 2, 2, 150, 6, 130, 6, 8, { 42, 0, 0, 0 }, 15, 50, "Ogre Juggernaught" },
    /* 34 */ { NULL, W2_SKIP, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 0, NULL },
    /* 35 */ { "fire-breeze", FLY, 2, 2, 800, 14, 35, 5, 9, { 0, 0, 0, 0 }, 10, 13, "Deathwing" },
    /* 36 */ { NULL, W2_SKIP, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 0, NULL },
    /* 37 */ { NULL, W2_SKIP, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 0, NULL },
    /* 38 */ { "gnome-submarine", SHIP, 2, 2, 60, 7, 50, 4, 5, { 43, 0, 182, 526 }, 0, 10, "Gnomish Submarine" },
    /* 39 */ { "giant-turtle", SHIP, 2, 2, 60, 7, 50, 4, 5, { 44, 0, 183, 527 }, 0, 10, "Giant Turtle" },
    /* 40 */ { "balloon", BALLOON, 2, 2, 150, 17, 0, 1, 9, { 38, 0, 0, 0 }, 2, 0, "Gnomish Flying Machine" },
    /* 41 */ { "zeppelin", BALLOON, 2, 2, 150, 17, 0, 1, 9, { 63, 0, 0, 0 }, 2, 0, "Goblin Zeppelin" },
    /* 42 */ { "gryphon-rider", FLY, 2, 2, 100, 14, 16, 4, 6, { 35, 0, 0, 0 }, 5, 8, NULL },
    /* 43 */ { "dragon", FLY, 2, 2, 100, 14, 16, 4, 6, { 36, 0, 0, 0 }, 5, 8, NULL },
    /* 44 */ { "knight-rider", LAND, 1, 1, 180, 13, 19, 1, 6, { 51, 0, 0, 0 }, 10, 3, "Turalyon" },
    /* 45 */ { "eye-of-kilrogg", FLY, 1, 1, 100, 42, 1, 1, 3, { 37, 0, 0, 0 }, 0, 1, NULL },
    /* 46 */ { "arthor-literios", LAND, 1, 1, 220, 10, 23, 1, 6, { 45, 0, 0, 0 }, 8, 4, "Danath" },
    /* 47 */ { "quick-blade", LAND, 1, 1, 240, 10, 22, 1, 5, { 46, 0, 0, 0 }, 8, 3, "Kargath Bladefist" },
    /* 48 */ { NULL, W2_SKIP, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 0, NULL },
    /* 49 */ { "double-head", LAND, 1, 1, 100, 13, 15, 1, 5, { 52, 0, 0, 0 }, 0, 3, "Cho'gall" },
    /* 50 */ { "wise-man", LAND, 1, 1, 90, 13, 12, 1, 5, { 51, 0, 0, 0 }, 4, 2, "Lothar" },
    /* 51 */ { "ice-bringer", LAND, 1, 1, 40, 8, 3, 3, 8, { 58, 0, 0, 0 }, 0, 2, "Gul'dan" },
    /* 52 */ { "man-of-light", LAND, 1, 1, 90, 13, 12, 1, 5, { 51, 0, 0, 0 }, 4, 2, "Uther Lightbringer" },
    /* 53 */ { "sharp-axe", LAND, 1, 1, 120, 10, 28, 5, 9, { 54, 0, 0, 0 }, 5, 9, "Zul'jin" },
    /* 54 */ { NULL, W2_SKIP, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 0, NULL },
    /* 55 */ { "skeleton", LAND, 1, 1, 40, 8, 9, 1, 3, { 69, 0, 0, 0 }, 0, 2, NULL },
    /* 56 */ { "daemon", LAND, 1, 1, 60, 14, 12, 3, 5, { 70, 0, 0, 0 }, 3, 1, NULL },
    /* 57 */ { "critter", CRIT, 1, 1, 5, 3, 0, 1, 2, { 64, 66, 65, 65 }, 0, 0, NULL },
    /* 58 */ { "farm", BLD, 2, 2, 400, 0, 0, 0, 1, { 92, 134, 173, 479 }, 20, 0, NULL },
    /* 59 */ { "pig-farm", BLD, 2, 2, 400, 0, 0, 0, 2, { 93, 135, 174, 480 }, 20, 0, NULL },
    /* 60 */ { "human-barracks", BLD, 3, 3, 800, 0, 0, 0, 1, { 94, 136, 94, 481 }, 20, 0, "Barracks" },
    /* 61 */ { "orc-barracks", BLD, 3, 3, 800, 0, 0, 0, 1, { 95, 137, 95, 482 }, 20, 0, "Barracks" },
    /* 62 */ { "church", BLD, 3, 3, 700, 0, 0, 0, 1, { 96, 138, 96, 483 }, 20, 0, NULL },
    /* 63 */ { "altar-of-storms", BLD, 3, 3, 700, 0, 0, 0, 1, { 97, 139, 97, 484 }, 20, 0, NULL },
    /* 64 */ { "human-watch-tower", BLD, 2, 2, 100, 0, 0, 0, 9, { 98, 140, 98, 485 }, 20, 0, "Scout Tower" },
    /* 65 */ { "orc-watch-tower", BLD, 2, 2, 100, 0, 0, 0, 9, { 99, 141, 99, 486 }, 20, 0, "Watch Tower" },
    /* 66 */ { "stables", BLD, 3, 3, 500, 0, 0, 0, 1, { 104, 146, 104, 491 }, 20, 0, NULL },
    /* 67 */ { "ogre-mound", BLD, 3, 3, 500, 0, 0, 0, 1, { 105, 147, 105, 492 }, 20, 0, NULL },
    /* 68 */ { "inventor", BLD, 3, 3, 500, 0, 0, 0, 1, { 90, 132, 90, 477 }, 20, 0, "Gnomish Inventor" },
    /* 69 */ { "alchemist", BLD, 3, 3, 500, 0, 0, 0, 1, { 91, 133, 91, 478 }, 20, 0, "Goblin Alchemist" },
    /* 70 */ { "gryphon-aviary", BLD, 3, 3, 500, 0, 0, 0, 1, { 88, 130, 88, 475 }, 20, 0, NULL },
    /* 71 */ { "dragon-roost", BLD, 3, 3, 500, 0, 0, 0, 1, { 89, 131, 89, 476 }, 20, 0, NULL },
    /* 72 */ { "human-shipyard", BLD, 3, 3, 1100, 0, 0, 0, 1, { 108, 150, 108, 495 }, 20, 0, "Shipyard" },
    /* 73 */ { "orc-shipyard", BLD, 3, 3, 1100, 0, 0, 0, 1, { 109, 151, 109, 496 }, 20, 0, "Shipyard" },
    /* 74 */ { "town-hall", HALL, 4, 4, 1200, 0, 0, 0, 1, { 100, 142, 100, 487 }, 20, 0, NULL },
    /* 75 */ { "great-hall", HALL, 4, 4, 1200, 0, 0, 0, 1, { 101, 143, 101, 488 }, 20, 0, NULL },
    /* 76 */ { "elven-lumber-mill", BLD, 3, 3, 600, 0, 0, 0, 1, { 102, 144, 175, 489 }, 20, 0, NULL },
    /* 77 */ { "troll-lumber-mill", BLD, 3, 3, 600, 0, 0, 0, 1, { 103, 145, 176, 490 }, 20, 0, NULL },
    /* 78 */ { "human-foundry", BLD, 3, 3, 750, 0, 0, 0, 1, { 110, 152, 110, 497 }, 20, 0, "Foundry" },
    /* 79 */ { "orc-foundry", BLD, 3, 3, 750, 0, 0, 0, 1, { 111, 153, 111, 498 }, 20, 0, "Foundry" },
    /* 80 */ { "mage-tower", BLD, 3, 3, 500, 0, 0, 0, 1, { 84, 160, 84, 505 }, 20, 0, NULL },
    /* 81 */ { "temple-of-the-damned", BLD, 3, 3, 500, 0, 0, 0, 1, { 85, 161, 85, 506 }, 20, 0, NULL },
    /* 82 */ { "human-blacksmith", BLD, 3, 3, 775, 0, 0, 0, 1, { 106, 148, 106, 493 }, 20, 0, "Blacksmith" },
    /* 83 */ { "orc-blacksmith", BLD, 3, 3, 775, 0, 0, 0, 1, { 107, 149, 107, 494 }, 20, 0, "Blacksmith" },
    /* 84 */ { "human-refinery", BLD, 3, 3, 600, 0, 0, 0, 1, { 112, 154, 112, 499 }, 20, 0, "Oil Refinery" },
    /* 85 */ { "orc-refinery", BLD, 3, 3, 600, 0, 0, 0, 1, { 113, 155, 113, 500 }, 20, 0, "Oil Refinery" },
    /* 86 */ { "human-oil-platform", BLD, 3, 3, 650, 0, 0, 0, 1, { 114, 156, 177, 501 }, 20, 0, "Oil Platform" },
    /* 87 */ { "orc-oil-platform", BLD, 3, 3, 650, 0, 0, 0, 1, { 115, 157, 178, 502 }, 20, 0, "Oil Platform" },
    /* 88 */ { "keep", HALL, 4, 4, 1400, 0, 0, 0, 3, { 86, 128, 86, 473 }, 20, 0, NULL },
    /* 89 */ { "stronghold", HALL, 4, 4, 1400, 0, 0, 0, 2, { 87, 129, 87, 474 }, 20, 0, NULL },
    /* 90 */ { "castle", HALL, 4, 4, 1600, 0, 0, 0, 6, { 116, 158, 116, 503 }, 20, 0, NULL },
    /* 91 */ { "fortress", HALL, 4, 4, 1600, 0, 0, 0, 6, { 117, 159, 117, 504 }, 20, 0, NULL },
    /* 92 */ { "gold-mine", BLD, 3, 3, 25500, 0, 0, 0, 1, { 119, 162, 179, 511 }, 20, 0, NULL },
    /* 93 */ { "oil-patch", BLD, 3, 3, 0, 0, 0, 0, 0, { 118, 118, 180, 515 }, 0, 0, NULL },
    /* 94 */ { "human-start", W2_SKIP, 1, 1, 0, 0, 0, 0, 0, { 164, 0, 0, 0 }, 0, 0, NULL },
    /* 95 */ { "orc-start", W2_SKIP, 1, 1, 0, 0, 0, 0, 0, { 165, 0, 0, 0 }, 0, 0, NULL },
    /* 96 */ { "human-guard-tower", BLD, 2, 2, 130, 0, 16, 6, 9, { 80, 169, 80, 507 }, 20, 6, "Guard Tower" },
    /* 97 */ { "orc-guard-tower", BLD, 2, 2, 130, 0, 16, 6, 9, { 81, 170, 81, 508 }, 20, 6, "Guard Tower" },
    /* 98 */ { "human-cannon-tower", BLD, 2, 2, 160, 0, 50, 7, 9, { 82, 171, 82, 509 }, 20, 20, "Cannon Tower" },
    /* 99 */ { "orc-cannon-tower", BLD, 2, 2, 160, 0, 50, 7, 9, { 83, 172, 83, 510 }, 20, 20, "Cannon Tower" },
    /* 100 */ { "circle-of-power", BLD, 2, 2, 0, 0, 0, 0, 0, { 166, 166, 166, 525 }, 0, 0, NULL },
    /* 101 */ { "dark-portal", BLD, 4, 4, 5000, 0, 0, 0, 4, { 167, 184, 185, 513 }, 0, 0, NULL },
    /* 102 */ { "runestone", BLD, 2, 2, 5000, 0, 0, 0, 4, { 181, 186, 181, 514 }, 20, 0, NULL },
    /* 103 */ { "human-wall", W2_SKIP, 1, 1, 40, 0, 0, 0, 1, { 0, 0, 0, 0 }, 20, 0, NULL },
    /* 104 */ { "orc-wall", W2_SKIP, 1, 1, 40, 0, 0, 0, 1, { 0, 0, 0, 0 }, 20, 0, NULL },
};

int w2_pud_named(const char *name) {
    if (!name || name[0] == '\0') return -1;
    for (int i = 0; i < W2_TYPE_COUNT; ++i)
        if (w2_units[i].name && strcmp(w2_units[i].name, name) == 0) return i;
    return -1;
}
