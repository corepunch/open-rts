/* Single-player production order, ZED.EXE 0x12ecb9 indices 0..16.
 * Availability comes from the group's ROBOTS.DAT bytes +73/+74/+75.
 * Existing mobile-unit seconds remain engine-authored. New gun stock
 * values were evaluated from 0x98acc using ROBOTS.DAT and MULT.DAT;
 * their duration is stock * 221 / CPUPLR's speed in native ticks, at 17 Hz.
 * Fire Truck and Truck are outside the single-player production cycle. */
/*        group type         seconds stock */
Z_PRODUCT(10, GATLING,         0, 1468)
Z_PRODUCT( 0, LIGHT_GUN,       0, 1746)
Z_PRODUCT(14, HOWITZER,        0, 2610)
Z_PRODUCT( 8, MISSILE_GUN,     0, 3451)
Z_PRODUCT( 2, GRUNT,          20,    0)
Z_PRODUCT( 6, PSYCHO,         25,    0)
Z_PRODUCT( 3, TOUGH,          30,    0)
Z_PRODUCT( 4, SNIPER,         35,    0)
Z_PRODUCT(17, PYRO,           40,    0)
Z_PRODUCT(18, LASER,          45,    0)
Z_PRODUCT(11, JEEP,           40,    0)
Z_PRODUCT(13, APC,            60,    0)
Z_PRODUCT( 7, LIGHT_TANK,     50,    0)
Z_PRODUCT( 1, MED_TANK,       70,    0)
Z_PRODUCT( 5, HEAVY_TANK,     90,    0)
Z_PRODUCT( 9, M_MISSILE,      80,    0)
Z_PRODUCT(16, CRANE,          60,    0)
