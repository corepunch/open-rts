/* What Z's forts and factories make, in the order their production
 * windows offer them. ZED.EXE's own tables (do_robot_factory,
 * do_tank_factory and the fort's sequence) are not traced, so who makes
 * what and how long it takes are authored: a fort makes robots and
 * vehicles, a robot factory robots and a vehicle factory vehicles. Guns
 * (Light Gun, Missile, Gatling, Howitzer), the Fire Truck and the Truck
 * are not manufactured yet. `seconds` is the time while the team holds
 * no territory; p_prod.c shortens it by the share of territories held. */
/*        type        makers                  seconds */
Z_PRODUCT(GRUNT,      ZM_FORT | ZM_ROBOTS,    20)
Z_PRODUCT(PSYCHO,     ZM_FORT | ZM_ROBOTS,    25)
Z_PRODUCT(SNIPER,     ZM_FORT | ZM_ROBOTS,    35)
Z_PRODUCT(TOUGH,      ZM_FORT | ZM_ROBOTS,    30)
Z_PRODUCT(LASER,      ZM_FORT | ZM_ROBOTS,    45)
Z_PRODUCT(PYRO,       ZM_FORT | ZM_ROBOTS,    40)
Z_PRODUCT(JEEP,       ZM_FORT | ZM_VEHICLES,  40)
Z_PRODUCT(LIGHT_TANK, ZM_FORT | ZM_VEHICLES,  50)
Z_PRODUCT(MED_TANK,   ZM_FORT | ZM_VEHICLES,  70)
Z_PRODUCT(HEAVY_TANK, ZM_FORT | ZM_VEHICLES,  90)
Z_PRODUCT(APC,        ZM_FORT | ZM_VEHICLES,  60)
Z_PRODUCT(M_MISSILE,  ZM_FORT | ZM_VEHICLES,  80)
Z_PRODUCT(CRANE,      ZM_FORT | ZM_VEHICLES,  60)
