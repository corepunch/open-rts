#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* A projectile remembers whom it was fired at (a mobj id), to hit it on arrival. */
/* A construction site remembers how far it has built (per mille), to grow its hit points. */
#define MOBJ_GAME_FIELDS uint32_t rf_victim; int rf_built;

#endif
