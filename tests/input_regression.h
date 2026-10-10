#ifndef __INPUT_REGRESSION__
#define __INPUT_REGRESSION__

#include "t_local.h"
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
    assert(g_ruleset.policy.input == INPUT_LEFT_SELECT_ORDER);
    gameinfo_t game = *gameinfo;
    level.width = level.height = 16;
    level.blocked = calloc(16 * 16, 1);
    level.resource_vents = calloc(1, sizeof(*level.resource_vents));
    assert(level.blocked && level.resource_vents);
    level.resource_vent_count = 1;
    level.resource_vents[0] = (resourcevent_t){.cell = {12, 12},
        .attachment = FIXED2_LIT(12.5, 12.5), .active = true, .rate = 1, .amount = 100};
    P_InitThinkers();
    app_t app = {.win = {512, 512}, .cell = {32, 32}};
    mobj_t *first = spawn_mobj_fixture((mobj_t){.hp = 100, .radius = FIXED_LIT(0.4),
        .traits = MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE | MF_ATTACK | MF_HARVESTER});
    first->core.position = fixed3_from_fixed2(FIXED2_LIT(3.5, 3.5), 0);
    mobj_t *second = spawn_mobj_fixture(*first), *enemy = spawn_mobj_fixture(*first);
    second->core.position = fixed3_from_fixed2(FIXED2_LIT(6.5, 3.5), 0);
    enemy->core.position = fixed3_from_fixed2(FIXED2_LIT(9.5, 6.5), 0);
    enemy->owner = 1;
    enemy->team = 1;
    enemy->allegiance = ALLEGIANCE_ENEMY;
    mobj_t *units[] = {first, second, enemy};
    ivec2_t a = screen_point(&app, fixed3_xy_to_fvec2(first->core.position));
    ivec2_t b = screen_point(&app, fixed3_xy_to_fvec2(second->core.position));
    ivec2_t foe = screen_point(&app, fixed3_xy_to_fvec2(enemy->core.position));
    fixed2_t destination = FIXED2_LIT(8.5, 10.5);
    ivec2_t ground = screen_point(&app, fvec2_from_fixed2(destination));
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(!first->movement.order_id && !P_MobjIsSelected(first));
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    assert(P_MobjIsSelected(first) && !P_MobjIsSelected(second));
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(P_MobjIsSelected(first) && first->movement.order_id);
    assert(fixed2_near(first->movement.goal, destination, FIXED_LIT(0.001)));
    uint32_t order = first->movement.order_id;
    click(&app, &game, units, SDL_BUTTON_RIGHT, foe);
    assert(!P_MobjIsSelected(first) && !first->attack.target);
    assert(first->movement.order_id == order); /* Deselect does not stop movement. */

    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_LEFT, foe);
    assert(first->attack.target == enemy && P_MobjIsSelected(first));
    click(&app, &game, units, SDL_BUTTON_LEFT,
          screen_point(&app, fvec2_from_fixed2(level.resource_vents[0].attachment)));
    assert(first->harvest.target == 0 && first->harvest.phase != 0 && !first->attack.target);
    click(&app, &game, units, SDL_BUTTON_RIGHT, ground);
    assert(!P_MobjIsSelected(first) && first->harvest.target == 0);

    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_LEFT, b);
    assert(!P_MobjIsSelected(first) && P_MobjIsSelected(second));
    SDL_SetModState(KMOD_SHIFT);
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    assert(P_MobjIsSelected(first) && P_MobjIsSelected(second));
    order = first->movement.order_id;
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(P_MobjIsSelected(first) && first->movement.order_id == order);
    SDL_SetModState(KMOD_NONE);

    /* Dragging replaces selection even when units were already selected. */
    button(&app, &game, units, SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, ivec2_sub(a, (ivec2_t){20,20}));
    button(&app, &game, units, SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, ivec2_add(b, (ivec2_t){20,20}));
    assert(P_MobjIsSelected(first) && P_MobjIsSelected(second));
    assert(!P_MobjIsSelected(enemy) && first->movement.order_id == order);
    assert(!app.dragging_select && !app.selection_rect.w && !app.selection_rect.h);

    button(&app, &game, units, SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_RIGHT, a);
    button(&app, &game, units, SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, a);
    assert(!app.dragging_select && !P_MobjIsSelected(first) && !P_MobjIsSelected(second));

    /* Warcraft/StarCraft can opt into the existing right-order convention. */
    ruleset_t right_click = g_ruleset;
    right_click.policy.input = INPUT_RIGHT_CLICK_ORDERS;
    R_RulesOverride(&right_click);
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    order = first->movement.order_id;
    click(&app, &game, units, SDL_BUTTON_LEFT, ground);
    assert(!P_MobjIsSelected(first) && first->movement.order_id == order);
    click(&app, &game, units, SDL_BUTTON_LEFT, a);
    click(&app, &game, units, SDL_BUTTON_RIGHT, ground);
    assert(P_MobjIsSelected(first) && first->movement.order_id != order);
    assert(fixed2_near(first->movement.goal, destination, FIXED_LIT(0.001)));
    click(&app, &game, units, SDL_BUTTON_RIGHT, foe);
    assert(first->attack.target == enemy);
    R_RulesOverride(NULL);

    P_FreeLevel(&level);
    SDL_Quit();
    printf("PASS: %s left select/move/attack/harvest, right cancel, drag/shift and alternate controls\n", g_game_id);
    return 0;
}

#endif
