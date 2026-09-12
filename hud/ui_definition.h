#ifndef __UI_DEFINITION__
#define __UI_DEFINITION__

#include "m_vec.h"

#include <SDL.h>
#define RTS_UI_MAX_LAYERS 16
#define RTS_UI_MAX_RESOURCES 8

typedef struct uiimage_s {
    const char *asset_path;
    irect_t source;
    irect_t destination;
} uiimage_t;

typedef struct uiresource_s {
    SDL_Point text; /* amount anchor in logical UI coordinates */
    SDL_Color color;
    /* The native UI may center a counter (false) or pin its right edge (true). */
    bool right_aligned;
} uiresource_t;

typedef struct uipanel_s {
    irect_t rect;
    SDL_Color fill;
    SDL_Color border;
} uipanel_t;

typedef struct uiproduct_s {
    int id;
    int category;
    const char *image;
} uiproduct_t;

typedef struct uicategory_s {
    const char *label;
    irect_t rect;
    int image;
    irect_t source;
} uicategory_t;

typedef enum { UI_UNAVAILABLE, UI_MOVE, UI_ATTACK, UI_STOP, UI_RADAR, UI_OPTIONS, UI_PRODUCT } uiactionkind_t;
typedef struct uiaction_s {
    const char *label;
    uiactionkind_t action;
    irect_t rect;
    int image;
    irect_t source;
    int product;
} uiaction_t;
typedef enum { UI_PALETTE_NONE, UI_PALETTE_OPENDR, UI_PALETTE_OPENKRUSH } uipalettetype_t;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct uidefinition_s {
    int logical_width;
    int logical_height;
    irect_t world_viewport;
    irect_t minimap;
    irect_t command_grid;
    int command_columns;
    int command_rows;
    uiresource_t resources[RTS_UI_MAX_RESOURCES];
    int resource_count;
    uipanel_t status_panel;
    bool status_elapsed_time;
    uipanel_t sidebar_panel;
    int sidebar_cell_size;
    const uiimage_t *images;
    int image_count;
    const char *asset_root;
    const char *palette;
    const uiproduct_t *products;
    int product_count;
    const uicategory_t *categories;
    int category_count;
    isize2_t icon_size;
    const uiaction_t *actions;
    int action_count;
    int minimap_scale;
    uipalettetype_t palette_type;
} uidefinition_t;

#endif
