#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* Lumber progress belongs to the worker, not the tree (Blizzard: 51 chops). */
#define MOBJ_GAME_FIELDS struct { int chops; } w2;
#define MOBJ_GAME_CHECKSUM(HASH, mo) HASH((mo)->w2.chops)

#endif
