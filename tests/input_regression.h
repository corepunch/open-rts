#ifndef __INPUT_REGRESSION__
#define __INPUT_REGRESSION__

#include "game.h"
#include <assert.h>

static void button(app_t *app, gameinfo_t *game, mobj_t **units,
                    Uint32 event, Uint8 button_id, ivec2_t point) {
    SDL_Event input = {.button = {.type = event, .button = button_id,
                                  .x = point.x, .y = point.y}};
    G_Responder(app, &level, units, 3, NULL, NULL, game, &input);
}

static void click(app_t *app, gameinfo_t *game, mobj_t **units,
                   Uint8 button_id, ivec2_t point) {
    button(app, game, units, SDL_MOUSEBUTTONDOWN, button_id, point);
    button(app, game, units, SDL_MOUSEBUTTONUP, button_id, point);
}

static ivec2_t screen_point(app_t *app, fvec2_t position) {
    fvec2_t screen;
    R_MapToScreen(app, &level, position.x, position.y, &screen.x, &screen.y);
    return (ivec2_t){(int)lroundf(screen.x), (int)lroundf(screen.y)};
}

int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    G_InitGame();
    assert(!gameinfo->right_click_orders);
    gameinfo_t game = *gameinfo;
    level.width = level.height = 16;
    level.blocked = calloc(16 * 16, 1);
    level.resource_vents = calloc(1, sizeof(*level.resource_vents));
    assert(level.blocked && level.resource_vents);
    level.resource_vent_count = 1;
    level.resource_vents[0] = (resourcevent_t){.cell = {12, 12},
        .attachment = {12.5f, 12.5f}, .active = true, .rate = 1, .amount = 100};
    P_InitThinkers();
    app_t app = {.win = {512, 512}, .cell = {32, 32}};
    mobj_t first = {.hp = 100, .radius = 0.4f,
        .traits = MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE | MF_ATTACK | MF_HARVESTER};
    first.core.position = fixed3_from_fvec2((fvec2_t){3.5f, 3.5f}, 0);
    mobj_t second = first, enemy = first;
    second.core.position = fixed3_from_fvec2((fvec2_t){6.5f, 3.5f}, 0);
    enemy.core.position = fixed3_from_fvec2((fvec2_t){9.5f, 6.5f}, 0);
    enemy.owner = 1;
    mobj_t *units[] = {&first, &second, &enemy};
    ivec2_t a = screen_point(&app, fixed3_xy_to_fvec2(first.core.position));
    ivec2_t b = screen_point(&app, fixed3_xy_to_fvec2(second.core.position));
    ivec2_t foe = screen_point(&app, fixed3_xy_to_fvec2(enemy.core.position));
    fvec2_t destination = {8.5f, 10.5f};
    ivec2_t ground = screen_point(&app, destination);
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(!first.movement.order_id && !P_MobjIsSelected(&first));
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    assert(P_MobjIsSelected(&first) && !P_MobjIsSelected(&second));
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(P_MobjIsSelected(&first) && first.movement.order_id);
    assert(fvec2_near(first.movement.goal, destination, 0.001f));
    uint32_t order = first.movement.order_id;
    click(&app, &game, units, SDL_BUTTON_RIGHT, foe);
    assert(!P_MobjIsSelected(&first) && !first.attack.target);
    assert(first.movement.order_id == order); /* Deselect does not stop movement. */

    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_LEFT, foe);
    assert(first.attack.target == &enemy && P_MobjIsSelected(&first));
    click(&app, &game, units, SDL_BUTTON_LEFT,
          screen_point(&app, level.resource_vents[0].attachment));
    assert(first.harvest.target == 0 && first.harvest.phase != 0 && !first.attack.target);
    click(&app, &game, units, SDL_BUTTON_RIGHT, ground);
    assert(!P_MobjIsSelected(&first) && first.harvest.target == 0);

    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_LEFT, b);
    assert(!P_MobjIsSelected(&first) && P_MobjIsSelected(&second));
    SDL_SetModState(KMOD_SHIFT);
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    assert(P_MobjIsSelected(&first) && P_MobjIsSelected(&second));
    order = first.movement.order_id;
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(P_MobjIsSelected(&first) && first.movement.order_id == order);
    SDL_SetModState(KMOD_NONE);

    /* Dragging replaces selection even when units were already selected. */
    button(&app, &game, units, SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, ivec2_sub(a, (ivec2_t){20,20}));
    button(&app, &game, units, SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, ivec2_add(b, (ivec2_t){20,20}));
    assert(P_MobjIsSelected(&first) && P_MobjIsSelected(&second));
    assert(!P_MobjIsSelected(&enemy) && first.movement.order_id == order);
    assert(!app.dragging_select && !app.selection_rect.w && !app.selection_rect.h);

    button(&app, &game, units, SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_RIGHT, a);
    button(&app, &game, units, SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, a);
    assert(!app.dragging_select && !P_MobjIsSelected(&first) && !P_MobjIsSelected(&second));

    /* Warcraft/StarCraft can opt into the existing right-order convention. */
    game.right_click_orders = true;
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    order = first.movement.order_id;
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(!P_MobjIsSelected(&first) && first.movement.order_id == order);
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_RIGHT, ground);
    assert(P_MobjIsSelected(&first) && first.movement.order_id != order);
    assert(fvec2_near(first.movement.goal, destination, 0.001f));
    click(&app, &game, units, SDL_BUTTON_RIGHT, foe);
    assert(first.attack.target == &enemy);

    P_FreeLevel(&level);
    SDL_Quit();
    printf("PASS: %s left select/move/attack/harvest, right cancel, drag/shift and alternate controls\n", g_game_id);
    return 0;
}

#endif
