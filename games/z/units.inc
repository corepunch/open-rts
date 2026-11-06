/* Z units by ROBOTS.DAT weapon type (the 42-byte records at 0x55f0, which a
 * robot names at +0x22 and OBJECTnn.DAT vehicles at byte 5). Every stat is
 * the retail value, decoded from ROBOTS.DAT as ZED.EXE reads it (see
 * docs/Z_EXE_FINDINGS.md, "Unit statistics"):
 *
 *   hp      robots: their first robot record's +0x20 (0x5108c);
 *           vehicles and guns: 25 * weapon +5 (0x50bfb, body +8).
 *   dmg     weapon +0x20 per shot or burst (unit +0x63, body +6).
 *   level   speed level on plain ground, robot +0x1f / 10 or weapon +4 / 10
 *           (0x79882); g_game.c turns it into pixels a step (0x117e5c).
 *   reach   pixels, 8 * (u16 weapon +2 >> 3).
 *   wait    native steps between shots, 100 - weapon +0x22 (0x60c4a). The
 *           Pyro pauses 16 steps after every 4 bursts: 6 on average.
 *
 * Kinds: ROBOT draws from the shared robot sheet; GUN turns through eight
 * directions from `sprite`, `turn` apart, `frames` long (0x14b10c actions
 * 0x32..0x34); VEHICLE is composed from parts in eight directions with
 * `frames` of track or wheel movement and `idle` standing (w_rsc.c),
 * and with `turret` a turret that aims on its own; STILL draws one
 * SPRITES.RSC picture. `dead` is the wreck. The Crane's weapon (120) is
 * never fired (0x58bea) and the Truck has none. */
/*     T  type         label               kind     mobile hp  dmg lvl reach wait sprite turn frames idle dead turret */
Z_UNIT( 0, LIGHT_GUN,  "Light Gun",        STILL,   0, 100,  75, 0, 120, 40,  352, 0, 1, 1, 5691, 0)
Z_UNIT( 1, MED_TANK,   "Medium Tank",      VEHICLE, 1, 200,  80, 4, 120, 30,    0, 0, 3, 1,  512, 1)
Z_UNIT( 2, GRUNT,      "Grunt",            ROBOT,   1,  30,   5, 4, 120, 20,    0, 0, 0, 1,    0, 0)
Z_UNIT( 3, TOUGH,      "Tough",            ROBOT,   1, 100,  40, 3, 120, 20,    0, 0, 0, 1,    0, 0)
Z_UNIT( 4, SNIPER,     "Sniper",           ROBOT,   1,  50,  10, 5, 144, 20,    0, 0, 0, 1,    0, 0)
Z_UNIT( 5, HEAVY_TANK, "Heavy Tank",       VEHICLE, 1, 300, 120, 3, 144, 40,    0, 0, 3, 1, 5317, 1)
Z_UNIT( 6, M_MISSILE,  "Mobile Missile",   VEHICLE, 1, 300, 120, 2, 160, 80,    0, 0, 3, 1, 5695, 1)
Z_UNIT( 7, PSYCHO,     "Psycho",           ROBOT,   1,  50,   2, 4, 120,  2,    0, 0, 0, 1,    0, 0)
Z_UNIT( 8, LIGHT_TANK, "Light Tank",       VEHICLE, 1, 100,  50, 5, 120, 20,    0, 0, 3, 1, 5747, 1)
Z_UNIT( 9, MISSILE_GUN,"Missile Launcher", GUN,     0, 100, 100, 0, 160, 70, 1643, 1, 1, 1, 5689, 0)
Z_UNIT(10, APC,        "A.P.C.",           VEHICLE, 1, 200,   5, 5, 120, 20,    0, 0, 3, 1, 5688, 0)
Z_UNIT(11, GATLING,    "Gatling",          GUN,     0,  50,   6, 0, 120,  3, 1659, 2, 2, 1, 5690, 0)
Z_UNIT(12, HOWITZER,   "Howitzer",         GUN,     0,  75, 100, 0, 200, 90, 5767, 2, 2, 1, 5694, 0)
Z_UNIT(13, JEEP,       "Jeep",             VEHICLE, 1,  50,   4, 6, 120,  2,    0, 0, 4, 2, 5687, 1)
Z_UNIT(14, FIRE_TRUCK, "Fire Truck",       STILL,   1, 200,  20, 5, 120,  2, 5215, 0, 1, 1, 5692, 0)
Z_UNIT(15, CRANE,      "Crane",            VEHICLE, 1, 300,   0, 5,   0,  0,    0, 0, 3, 1, 5696, 1)
Z_UNIT(16, TRUCK,      "Truck",            STILL,   1,  75,   0, 6,   0,  0, 5213, 0, 1, 1,   -1, 0)
Z_UNIT(17, PYRO,       "Pyro",             ROBOT,   1,  80,  20, 4, 120,  6,    0, 0, 0, 1,    0, 0)
Z_UNIT(18, LASER,      "Laser",            ROBOT,   1,  60,  15, 5, 136, 20,    0, 0, 0, 1,    0, 0)
