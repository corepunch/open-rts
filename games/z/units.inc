/* Z units by ROBOTS.DAT weapon type (the 42-byte records at 0x55f0, which a
 * robot names at +0x22 and OBJECTnn.DAT vehicles at byte 5). Damage is the
 * record's byte at +0x20 and vehicle speed its byte at +0x04 times 10/16
 * (`spd` is cells a second x 10); neither reading is confirmed against
 * ZED.EXE. Hit points, robot speed, range and reload are authored.
 *
 * Kinds: ROBOT draws from the shared robot sheet; GUN turns through eight
 * directions from `sprite`, `turn` apart, `frames` long (0x14b10c actions
 * 0x32..0x34); VEHICLE is composed from parts in eight directions with
 * `frames` of track or wheel movement (w_rsc.c); STILL draws one
 * SPRITES.RSC picture. `dead` is the wreck. */
/*     T  type         label               kind     mobile hp  dmg spd range reload sprite turn frames dead */
Z_UNIT( 0, LIGHT_GUN,  "Light Gun",        STILL,   0, 150,  75,  0, 6, 2000,  352, 0, 1, 5691)
Z_UNIT( 1, MED_TANK,   "Medium Tank",      VEHICLE, 1, 250,  80, 25, 6, 2000,    0, 0, 3,  512)
Z_UNIT( 2, GRUNT,      "Grunt",            ROBOT,   1,  40,   5, 20, 5,  600,    0, 0, 0,    0)
Z_UNIT( 3, TOUGH,      "Tough",            ROBOT,   1,  60,  40, 16, 6, 2000,    0, 0, 0,    0)
Z_UNIT( 4, SNIPER,     "Sniper",           ROBOT,   1,  40,  10, 20, 8, 1200,    0, 0, 0,    0)
Z_UNIT( 5, HEAVY_TANK, "Heavy Tank",       VEHICLE, 1, 350, 120, 19, 6, 2500,    0, 0, 3, 5317)
Z_UNIT( 6, M_MISSILE,  "Mobile Missile",   VEHICLE, 1, 200, 120, 12, 8, 3000,    0, 0, 3, 5695)
Z_UNIT( 7, PSYCHO,     "Psycho",           ROBOT,   1,  40,   2, 20, 4,  150,    0, 0, 0,    0)
Z_UNIT( 8, LIGHT_TANK, "Light Tank",       VEHICLE, 1, 150,  50, 31, 6, 1500,    0, 0, 3, 5747)
Z_UNIT( 9, MISSILE_GUN,"Missile Launcher", GUN,     0, 200, 100,  0, 8, 3000, 1643, 1, 1, 5689)
Z_UNIT(10, APC,        "A.P.C.",           VEHICLE, 1, 200,   5, 31, 4,  600,    0, 0, 3, 5688)
Z_UNIT(11, GATLING,    "Gatling",          GUN,     0, 150,   6,  0, 6,  200, 1659, 2, 2, 5690)
Z_UNIT(12, HOWITZER,   "Howitzer",         GUN,     0, 250, 100,  0,10, 3500, 5767, 2, 2, 5694)
Z_UNIT(13, JEEP,       "Jeep",             VEHICLE, 1, 100,   4, 37, 5,  300,    0, 0, 2, 5687)
Z_UNIT(14, FIRE_TRUCK, "Fire Truck",       STILL,   1, 200,  20, 31, 3,  600, 5215, 0, 1, 5692)
Z_UNIT(15, CRANE,      "Crane",            VEHICLE, 1, 200,   0, 31, 0,    0,    0, 0, 3, 5696)
Z_UNIT(16, TRUCK,      "Truck",            STILL,   1, 100,   0, 37, 0,    0, 5213, 0, 1,   -1)
Z_UNIT(17, PYRO,       "Pyro",             ROBOT,   1,  40,  20, 20, 2,  300,    0, 0, 0,    0)
Z_UNIT(18, LASER,      "Laser",            ROBOT,   1,  40,  15, 20, 6,  800,    0, 0, 0,    0)
