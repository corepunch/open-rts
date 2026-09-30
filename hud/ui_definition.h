#ifndef __UI_DEFINITION__
#define __UI_DEFINITION__

#include "m_vec.h"

#include <stdint.h>
#define RTS_UI_MAX_LAYERS 16
#define RTS_UI_MAX_RESOURCES 8

typedef struct uiimage_s {
    const char *asset_path;
    irect_t source;
    irect_t destination;
} uiimage_t;

typedef struct uiresource_s {
    ivec2_t text; /* amount anchor in logical UI coordinates */
    uint32_t color; /* 0xAARRGGBB, nearest palette index at draw time */
    /* The native UI may center a counter (false) or pin its right edge (true). */
    bool right_aligned;
} uiresource_t;

typedef struct uipanel_s {
    irect_t rect;
    uint32_t fill;
    uint32_t border;
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

typedef enum {
    UI_UNAVAILABLE, UI_MOVE, UI_ATTACK, UI_STOP, UI_RADAR, UI_OPTIONS, UI_PRODUCT,
    UI_PAGE, UI_WAYPOINT, UI_PATH_CLEAR, UI_PATH_DELETE, UI_PATH_GO,
    UI_PATH_SAVE, UI_PATH_DESELECT, UI_PATH_MODE, UI_PATH_ADVANCED
} uiactionkind_t;
typedef struct uiaction_s {
    const char *label;
    uiactionkind_t action;
    irect_t rect;
    int image;
    irect_t source;
    int product;
} uiaction_t;

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
    const uiproduct_t *products;
    int product_count;
    const uicategory_t *categories;
    int category_count;
    isize2_t icon_size;
    const uiaction_t *actions;
    int action_count;
    int minimap_scale;
    const uiaction_t *path_actions;
    int path_action_count;
    irect_t path_list;
    int path_row_height;
    /* Native chrome the engine does not draw. Arguments are logical rects. */
    void (*draw_status)(const struct app_s *app, const struct level_s *map, irect_t rect);
    void (*draw_minimap_overlay)(const struct app_s *app, const struct level_s *map, irect_t rect);
    void (*draw_product_slot)(const struct app_s *app, int product, irect_t rect);
} uidefinition_t;

#endif
