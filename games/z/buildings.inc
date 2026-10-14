/* ZED.EXE building types: 74-byte records at image 0xe6120 (name, footprint in
 * cells at +0x30), indexed by BUILDnn.DAT byte 4. Sprites by world (LEVELS.DAT
 * +0x6e: desert, arctic, city, volcano, jungle) from the draw tables at
 * 0x1426d0 (repair), 0x1426e4 (robot), 0x1426f8 (tank) and 0x14270c (radar).
 * Forts (type 2) come from the map, not BUILDnn.DAT; see w_lvl.c. */
Z_BUILDING(0, REPAIR,        "Vehicle Repair Facility",      5, 4, 2125, 2962, 3095, 4024, 2823)
Z_BUILDING(1, TANK_FACTORY,  "Tank Manufacturing Facility",  4, 5, 2089, 2960, 3093, 4022, 2821)
Z_BUILDING(4, ROBOT_FACTORY, "Robot Manufacturing Facility", 4, 5, 2090, 2961, 3094, 4023, 2822)
Z_BUILDING(8, RADAR,         "Radar Station",                4, 3, 2126, 2963, 3096, 4025, 2824)
