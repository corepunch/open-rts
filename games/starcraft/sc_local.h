#ifndef __SC_LOCAL__
#define __SC_LOCAL__
#include "engine.h"
#include "starcraft.h"
#include "info.h"
typedef struct {
    const char *name;
    int hp;
    uint32_t flags;
    isize2_t placement;
    int sight, orders, race, minerals, gas, portrait;
} sc_unit_t;
extern const sc_unit_t sc_units[SC_TYPES];
extern char sc_names[SC_TYPES][16];
extern uint32_t sc_palette[256];
bool sc_read(const char *root, const char *name, blob_t *out);
bool sc_load_graphics(const char *root, spritecache_t *cache);
bool sc_load_tiles(const char *root, tileset_t *out);
bool sc_portrait(const char *root,int id,char *path,size_t size);
bool sc_dialog(const char *root,const char *path,sc_dialog_t *out);
bool sc_font_colors(const char *root,const char *path,bitmapfont_t *font);
void sc_init_info(void);
bool sc_movie(const char *path, spritesheet_t *out, unsigned *frame_ms, uint32_t **palettes);
bool sc_font(const char *root, const char *name, bitmapfont_t *font);
#endif
