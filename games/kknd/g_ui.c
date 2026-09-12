#include "game.h"
#include "kknd.h"

/* OpenKrush SidebarWidget / SidebarButtonWidget: 48-pixel cells. */
static const uiimage_t images[] = {
    {"buttons.png", {0}, {0}},
    {"survivors.png", {0}, {0}},
    {"evolved.png", {0}, {0}},
};
static const uicategory_t categories[] = {
    {"Infantry", {912,0,48,48}, 1, {0,0,48,48}},
    {"Vehicles", {912,48,48,48}, 1, {48,0,48,48}},
    {"Buildings", {912,96,48,48}, 1, {144,0,48,48}},
    {"Towers", {912,144,48,48}, 1, {192,0,48,48}},
    {"Walls", {912,192,48,48}, 1, {240,0,48,48}},
};
static const uiproduct_t products[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) {id,category,#id ".png"},
#include "products.inc"
#undef KK_PRODUCT
};
static const uiaction_t actions[] = {
    {"Bomber - unavailable", UI_UNAVAILABLE, {912,288,48,48}, 1, {96,0,48,48}, 0},
    {"Sell - unavailable", UI_UNAVAILABLE, {912,384,48,48}, 0, {336,0,48,48}, 0},
    {"Research - click a building; click again to cancel", UI_PRODUCT, {912,432,48,48}, 0, {48,48,48,48}, KKND_RESEARCH},
    {"Repair - unavailable", UI_UNAVAILABLE, {912,480,48,48}, 0, {336,48,48,48}, 0},
    {"Radar", UI_RADAR, {912,576,48,48}, 0, {192,0,48,48}, 0},
    {"Options", UI_OPTIONS, {912,624,48,48}, 0, {240,0,48,48}, 0},
};
static const uidefinition_t ui = {
    .palette_type = UI_PALETTE_OPENKRUSH,
    .logical_width = 960, .logical_height = 720,
    .world_viewport = {0,0,912,720},
    .command_grid = {864,0,48,672}, .command_columns = 1, .command_rows = 14,
    .icon_size = {48,48},
    .resources = {{.text = {560,10}, .color = {255,255,255,255}, .right_aligned = true}},
    .resource_count = 1,
    .status_panel = {{390,0,180,28}, {0,0,0,255}, {255,255,255,255}},
    .status_elapsed_time = true,
    .sidebar_panel = {{912,0,48,720}, {0,0,0,255}, {0,0,0,255}},
    .images = images, .image_count = sizeof(images)/sizeof(*images),
    .asset_root = "games/kknd/ui",
    .palette = "games/kknd/ui/palette.png",
    .products = products, .product_count = sizeof(products)/sizeof(*products),
    .categories = categories, .category_count = sizeof(categories)/sizeof(*categories),
    .actions = actions, .action_count = sizeof(actions)/sizeof(*actions),
    .minimap_scale = 2,
};
const uidefinition_t *const gameui = &ui;
