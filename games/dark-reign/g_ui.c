#include "game.h"

/* OpenDR chrome/ingame-player.yaml and chrome.yaml, at 960 x 720. */
static const uiimage_t images[] = {
    {"sidebar.png", {0,167,238,329}, {710,10,238,329}},
    {"sidebar.png", {0,85,238,28}, {710,272,238,28}},
    {"sidebar.png", {0,546,238,6}, {710,634,238,6}},
    {"sidebar.png", {0,123,434,44}, {5,671,434,44}},
    {"sidebar.png", {0}, {0}}, /* atlas for buttons and icon rows */
    {"glyphs.png", {0}, {0}},
};
static const uicategory_t categories[] = {
    {"Economy", {717,342,28,28}, 5, {102,68,16,16}},
    {"Infantry", {717,373,28,28}, 5, {34,68,16,16}},
    {"Vehicles", {717,404,28,28}, 5, {51,68,16,16}},
    {"Upgrades", {717,435,28,28}, 5, {136,68,16,16}},
};
static const uiproduct_t products[] = {
    {10001,0,"bfhqtmn0.spr"}, {10002,3,"bfhqtmn0.spr"}, {10003,3,"bfhqtmn0.spr"},
    {10004,0,"bfutfmn0.spr"}, {10005,3,"bfutfmn0.spr"},
    {10006,0,"bfvcymn0.spr"}, {10007,3,"bfvcymn0.spr"},
    {10008,0,"bfhspmn0.spr"}, {10009,0,"bfrepmn0.spr"},
    {10010,0,"bccammn0.spr"}, {10011,0,"bfrrmmn0.spr"},
    {10012,0,"bfaarmn0.spr"}, {10013,0,"bfgdtmn0.spr"}, {10014,0,"bfagtmn0.spr"},
    {10015,0,"bfphfmn0.spr"}, {10016,3,"bfphfmn0.spr"},
    {10019,0,"bclncmn0.spr"}, {10020,0,"bcpowmn0.spr"},
    {10040,0,"bcsbhmn0.spr"}, {10041,0,"bcsbvmn0.spr"}, {10042,0,"bcsbcmn0.spr"},
    {11,0,"ucfcnmn0.spr"}, {9,1,"ufradmn0.spr"}, {10,1,"ufmrcmn0.spr"},
    {8,1,"ufsnpmn0.spr"}, {6,1,"ufsctmn0.spr"}, {7,1,"ufmedmn0.spr"},
    {3,1,"ufsabmn0.spr"}, {2,1,"ufmecmn0.spr"}, {5,1,"ufmtrmn0.spr"},
    {4,1,"ucinfmn0.spr"},
    {1,2,"ufspbmn0.spr"}, {15,2,"ufratmn0.spr"}, {20,2,"ufsktmn0.spr"},
    {17,2,"ufthnmn0.spr"}, {21,2,"ufphtmn0.spr"}, {12,2,"ufflkmn0.spr"},
    {16,2,"uftrtmn0.spr"}, {19,2,"uffarmn0.spr"}, {23,2,"ufskbmn0.spr"},
    {24,2,"ufoutmn0.spr"}, {18,2,"ufswvmn0.spr"}, {30,2,"ucwcomn0.spr"},
    {13,2,"ucfrgmn0.spr"}, {14,2,"uchfrmn0.spr"},
};
static const uiaction_t actions[] = {
    {"Beacon - unavailable", UI_UNAVAILABLE, {719,17,28,28}, 5, {153,68,16,16}, 0},
    {"Sell - unavailable", UI_UNAVAILABLE, {751,17,28,28}, 5, {119,68,16,16}, 0},
    {"Power - unavailable", UI_UNAVAILABLE, {783,17,28,28}, 5, {170,68,16,16}, 0},
    {"Repair - unavailable", UI_UNAVAILABLE, {815,17,28,28}, 5, {136,68,16,16}, 0},
    {"Options", UI_OPTIONS, {911,17,28,28}, 5, {102,68,16,16}, 0},
    {"Attack move - unavailable", UI_UNAVAILABLE, {14,680,34,26}, 5, {0,207,24,24}, 0},
    {"Move", UI_MOVE, {48,680,34,26}, 5, {25,207,24,24}, 0},
    {"Attack", UI_ATTACK, {82,680,34,26}, 5, {50,207,24,24}, 0},
    {"Guard - unavailable", UI_UNAVAILABLE, {116,680,34,26}, 5, {75,207,24,24}, 0},
    {"Deploy - unavailable", UI_UNAVAILABLE, {150,680,34,26}, 5, {100,207,24,24}, 0},
    {"Scatter - unavailable", UI_UNAVAILABLE, {184,680,34,26}, 5, {125,207,24,24}, 0},
    {"Stop", UI_STOP, {218,680,34,26}, 5, {150,207,24,24}, 0},
    {"Queue orders - unavailable", UI_UNAVAILABLE, {252,680,34,26}, 5, {175,207,24,24}, 0},
    {"Attack stance - unavailable", UI_UNAVAILABLE, {294,680,34,26}, 5, {0,119,16,16}, 0},
    {"Defend stance - unavailable", UI_UNAVAILABLE, {328,680,34,26}, 5, {17,119,16,16}, 0},
    {"Return fire - unavailable", UI_UNAVAILABLE, {362,680,34,26}, 5, {34,119,16,16}, 0},
    {"Hold fire - unavailable", UI_UNAVAILABLE, {396,680,34,26}, 5, {51,119,16,16}, 0},
};
static const uidefinition_t ui = {
    .palette_type = UI_PALETTE_OPENDR,
    .logical_width = 960, .logical_height = 720,
    .world_viewport = {0,0,710,720}, .minimap = {719,51,220,220},
    .command_grid = {750,340,192,294}, .command_columns = 3, .command_rows = 6,
    .icon_size = {64,49},
    .resources = {{.text = {795,282}, .color = {255,255,255,255}, .right_aligned = true}},
    .status_panel = {{710,272,238,28}, {0}, {0}}, .status_elapsed_time = true,
    .resource_count = 1,
    .images = images, .image_count = sizeof(images)/sizeof(*images),
    .asset_root = "games/dark-reign/ui",
    .products = products, .product_count = sizeof(products)/sizeof(*products),
    .categories = categories, .category_count = sizeof(categories)/sizeof(*categories),
    .actions = actions, .action_count = sizeof(actions)/sizeof(*actions),
};
const uidefinition_t *const gameui = &ui;
