#include "engine.h"

/* Engine fallback screen; native front-end reproduction is still pending. */
static menuitem_t items[] = {
    {.kind = MI_STATIC, .visible = true, .rect = {200,170,240,130}, .fill = 0xff0c1216u},
    {.kind = MI_STATIC, .visible = true, .rect = {200,175,240,30}, .text = "KKND", .centered = true},
    {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {220,215,200,30},
     .fill = 0xff18242du, .color = 0xffdce6dcu, .text = "START GAME", .centered = true, .routine = M_MenuBeginLevel},
    {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {220,255,200,30},
     .fill = 0xff18242du, .color = 0xffdce6dcu, .text = "QUIT GAME", .centered = true, .routine = M_MenuQuitGame},
};
menu_t gamemenu = {.items = items, .numitems = sizeof(items) / sizeof(*items)};
