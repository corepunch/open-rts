#define _DEFAULT_SOURCE
#include "sb_bar.h"
#include "info.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    irect_t outer;
    irect_t header;
    irect_t status;
    irect_t commands;
    irect_t money;
    irect_t days;
    irect_t minimap;
    irect_t message;
    irect_t build;
    irect_t tabs[3];
    irect_t buttons[8];
} UiLayout;

typedef struct {
    int id;
    int frame;
    char label[40];
} SidebarCommand;

typedef struct {
    SidebarCommand commands[6];
    int command_count;
} Sidebar;

typedef struct {
    bool active;
    bool font_ready;
    bitmapfont_t font;
    spritesheet_t background;
    Sidebar sidebar;
    uint64_t clock;
} sb_state_t;

typedef StaticProductDefinition ProductButton;

enum {
    CLIENT_PRODUCT_UNIT = 2,
    CLIENT_MT_TROOPER = 1,
    CLIENT_MT_REAPER = 4,
    CLIENT_MT_THUNDERBOLT = 5,
    CLIENT_MT_CYBORG = 6,
    CLIENT_MT_SCOUT = 7,
    CLIENT_MT_EXPLOITER = 3,
    CLIENT_MT_EXCOPOD = 1000,
    CLIENT_MT_BRRKPOD = 1001,
    CLIENT_MT_ROBOPOD = 1002,
    CLIENT_MT_ROBOPOD2 = 1003,
    CLIENT_MT_SCNCPOD = 1004,
    CLIENT_MT_SCNCPOD2 = 1005,
    CLIENT_MT_RSCHPOD = 1006,
    CLIENT_PRODUCTION_BUILD_GROUP = 6,
    CLIENT_TRSCBUILD_FIRST_FRAME = 12,
};

static void sidebar_defaults(Sidebar *sidebar) {
    if (!sidebar) return;
    const int ids[6] = { 150, 33, 35, 36, 37, 143 };
    const int frames[6] = { 62, 63, 65, 66, 74, 2 };
    const char *labels[6] = {
        "Stop",
        "Move Only",
        "Move & Attack",
        "Set waypoints",
        "Deploy",
        "Second Attack",
    };
    memset(sidebar, 0, sizeof(*sidebar));
    sidebar->command_count = 6;
    for (int i = 0; i < sidebar->command_count; ++i) {
        sidebar->commands[i].id = ids[i];
        sidebar->commands[i].frame = frames[i];
        snprintf(sidebar->commands[i].label, sizeof(sidebar->commands[i].label), "%s", labels[i]);
    }
}

static irect_t ui_rect(const app_t *app, int x, int y, int w, int h) {
    int win_w = app && app->win.w > 0 ? app->win.w : 640;
    int win_h = app && app->win.h > 0 ? app->win.h : 480;
    irect_t r = {
        x >= 516 ? win_w - (640 - x) : x,
        y >= 455 ? win_h - (480 - y) : y,
        w,
        h,
    };
    if (r.w < 1 && w > 0) r.w = 1;
    if (r.h < 1 && h > 0) r.h = 1;
    return r;
}

static SidebarCommand *sidebar_command(Sidebar *sidebar, int id) {
    if (!sidebar) return NULL;
    for (int i = 0; i < sidebar->command_count; ++i)
        if (sidebar->commands[i].id == id) return &sidebar->commands[i];
    return NULL;
}

static void sidebar_load(Sidebar *sidebar, const char *data_root) {
    if (!sidebar || !data_root) return;
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/MAINE");
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) {
        W_FreeFile(&blob);
        return;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next;
            *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        while (isspace((unsigned char)*line)) line++;
        if (*line != '%' && *line != '\0') {
            int id = 0;
            char label[40] = { 0 };
            if (sscanf(line, "textmsg %d %39[^\r\n]", &id, label) == 2) {
                SidebarCommand *cmd = sidebar_command(sidebar, id);
                if (cmd) {
                    size_t len = strlen(label);
                    while (len > 0 && isspace((unsigned char)label[len - 1])) label[--len] = '\0';
                    snprintf(cmd->label, sizeof(cmd->label), "%s", label);
                }
            } else {
                char kind[16] = { 0 };
                int desc = 0, x = 0, y = 0, w = 0, h = 0, frame = 0, pushed = 0;
                if (sscanf(line, "%15s %d %d %d %d %d %d %d %d",
                           kind, &id, &desc, &x, &y, &w, &h, &frame, &pushed) == 9 &&
                    (strcmp(kind, "pushb") == 0 || strcmp(kind, "checkb") == 0)) {
                    SidebarCommand *cmd = sidebar_command(sidebar, id);
                    if (cmd) cmd->frame = frame;
                }
            }
        }
        line = next;
    }
    free(text);
}

int SB_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    int w = app->win.w - 124;
    return w > 0 ? w : 1;
}

static UiLayout ui_layout(const app_t *app) {
    UiLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.outer = ui_rect(app, 516, 0, 124, 480);
    layout.minimap = ui_rect(app, 520, 5, 96, 84);
    layout.commands = ui_rect(app, 516, 92, 124, 330);
    layout.status = ui_rect(app, 518, 368, 59, 41);
    layout.money = ui_rect(app, 524, 456, 72, 17);
    layout.days = ui_rect(app, 613, 433, 3, 1);
    layout.message = ui_rect(app, 50, 462, 427, 11);
    layout.build = ui_rect(app, 516, 422, 86, 27);
    layout.header = ui_rect(app, 516, 0, 124, 92);
    layout.tabs[0] = ui_rect(app, 518, 92, 40, 20);
    layout.tabs[1] = ui_rect(app, 557, 92, 41, 20);
    layout.tabs[2] = ui_rect(app, 598, 92, 40, 20);

    const int button_y[6] = { 112, 153, 194, 235, 276, 317 };
    for (int i = 0; i < 6; ++i) {
        layout.buttons[i] = ui_rect(app, 518, button_y[i], 59, 41);
    }
    return layout;
}

static irect_t product_button_rect(const app_t *app, int index) {
    int col = index / 4;
    int row = index % 4;
    return ui_rect(app, 518 + col * 59, 112 + row * 41, 59, 41);
}

static void dc_ui_set_draw(SDL_Renderer *renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void dc_ui_fill(SDL_Renderer *renderer, irect_t rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

static void dc_ui_stroke(SDL_Renderer *renderer, irect_t rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

static void dc_ui_draw_sprite_fit(SDL_Renderer *renderer, const spritesheet_t *sprite, int frame,
                                  irect_t box, uint32_t render_flags) {
    if (!renderer || !sprite || !sprite->texture || sprite->frame_count <= 0) return;
    if (frame < 0 || frame >= sprite->frame_count) frame = 0;
    irect_t src = sprite->frames[frame];
    if (sprite->frame_bounds && sprite->frame_bounds[frame].w > 0 && sprite->frame_bounds[frame].h > 0) {
        irect_t bounds = sprite->frame_bounds[frame];
        src.x += bounds.x;
        src.y += bounds.y;
        src.w = bounds.w;
        src.h = bounds.h;
    }
    if (src.w <= 0 || src.h <= 0 || box.w <= 0 || box.h <= 0) return;
    int draw_w = box.w;
    int draw_h = src.h * draw_w / src.w;
    if (draw_h > box.h) {
        draw_h = box.h;
        draw_w = src.w * draw_h / src.h;
    }
    if (draw_w <= 0) draw_w = 1;
    if (draw_h <= 0) draw_h = 1;
    irect_t dst = {
        box.x + (box.w - draw_w) / 2,
        box.y + (box.h - draw_h) / 2,
        draw_w,
        draw_h,
    };
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(renderer, sprite->texture, &src, &dst, 0.0, NULL, flip);
}

static void dc_ui_draw_image_part(SDL_Renderer *renderer, const spritesheet_t *image,
                                  irect_t src, irect_t dst) {
    if (!renderer || !image || !image->texture || src.w <= 0 || src.h <= 0 ||
        dst.w <= 0 || dst.h <= 0) {
        return;
    }
    SDL_RenderCopy(renderer, image->texture, &src, &dst);
}

static const mobj_t *dc_first_selected_unit(const mobj_t *units, int unit_count) {
    if (!units) return NULL;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) return &units[i];
    }
    return NULL;
}

static bool dc_product_prerequisites_met(const mobj_t *units, int unit_count,
                                         const ProductButton *product) {
    return G_ModelProductAvailableForUnits(units, unit_count, product);
}

static bool dc_selected_unit_is_player_building(const mobj_t *selected) {
    return selected && selected->owner == 0 && !selected->remove && selected->hp > 0 &&
        selected->type_id >= CLIENT_MT_EXCOPOD;
}

static int dc_products_for_selected_building(const mobj_t *selected, const mobj_t *units,
                                             int unit_count,
                                             const ProductButton *out[8]) {
    if (!dc_selected_unit_is_player_building(selected) || !out) return 0;
    int count = 0;
    static ProductButton products[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    int source_count = G_ModelGetProducts(NULL, 0, products,
                                          (int)(sizeof(products) / sizeof(products[0])));
    for (int i = 0; i < source_count && count < 8; ++i) {
        const ProductButton *product = &products[i];
        bool this_maker = false;
        for (int maker = 0; maker < product->maker_count; ++maker) {
            if (product->makers[maker] == (int)selected->type_id) {
                this_maker = true;
                break;
            }
        }
        if (!this_maker) continue;
        if (!dc_product_prerequisites_met(units, unit_count, product)) continue;
        out[count++] = product;
    }
    return count;
}

static const ProductButton *dc_product_by_type(int product_type) {
    return G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, product_type);
}

static bool dc_enqueue_unit_product(mobj_t *producer, const ProductButton *product,
                                    uint16_t actor_id) {
    if (!producer || !product || actor_id == 0) return false;
    if (producer->production.queue_count > 0) {
        if (producer->production.actor_id != actor_id ||
            producer->production.product_type != product->product_type ||
            producer->production.product_class != CLIENT_PRODUCT_UNIT ||
            producer->production.queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        producer->production.queue_count++;
        return true;
    }
    producer->production.actor_id = actor_id;
    producer->production.product_class = CLIENT_PRODUCT_UNIT;
    producer->production.product_type = product->product_type;
    producer->production.queue_count = 1;
    producer->production.time_ms = G_ModelProductTrainingTimeMs(product);
    producer->production.time_left_ms = producer->production.time_ms;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    return true;
}

static const state_t *dc_state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int dc_find_state_by_group_frame(const gameinfo_t *game_info, int group, int frame) {
    if (!game_info || !game_info->states) return -1;
    for (int i = 0; i < game_info->state_count; ++i) {
        const state_t *state = &game_info->states[i];
        if (state->misc1 != group || state->frame != frame) continue;
        return i;
    }
    return -1;
}

static int dc_state_chain_duration_ms(const gameinfo_t *game_info, int state_id, int group) {
    int tics = 0;
    int guard = 0;
    while (guard++ < (game_info ? game_info->state_count + 1 : 1)) {
        const state_t *state = dc_state_at(game_info, state_id);
        if (!state || state->misc1 != group) break;
        if (state->tics > 0) tics += state->tics;
        int next = state->nextstate;
        if (next == game_info->null_state || next == state_id) break;
        state_id = next;
    }
    if (tics <= 0) return 0;
    return (int)(tics * FIXED_DT * 1000.0f + 0.5f);
}

static bool dc_product_uses_barracks_release(const mobj_t *producer,
                                             const ProductButton *product,
                                             uint16_t actor_id) {
    return producer && product && producer->type_id == CLIENT_MT_BRRKPOD &&
        product->product_type == 0 && actor_id == CLIENT_MT_TROOPER;
}

static bool dc_start_production_release(level_t *map,
                                        effect_t *effects, int max_effects,
                                        mobj_t *producer,
                                        const ProductButton *product,
                                        uint16_t actor_id) {
    if (!gameinfo || !producer || !product) return false;
    if (!dc_product_uses_barracks_release(producer, product, actor_id)) return false;
    int state_id = dc_find_state_by_group_frame(gameinfo, CLIENT_PRODUCTION_BUILD_GROUP,
                                                CLIENT_TRSCBUILD_FIRST_FRAME);
    int duration_ms = dc_state_chain_duration_ms(gameinfo, state_id,
                                                 CLIENT_PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    statecontext_t ctx = {
        .map = map,
        .effects = effects,
        .max_effects = max_effects,
        .game_info = gameinfo,
    };
    if (!P_SetMobjState(&ctx, producer, state_id)) return false;
    producer->production.release_active = true;
    producer->production.release_time_left_ms = duration_ms;
    producer->production.time_left_ms = 0;
    return true;
}

static void dc_clear_production(mobj_t *producer) {
    if (!producer) return;
    producer->production.actor_id = 0;
    producer->production.product_class = 0;
    producer->production.product_type = 0;
    producer->production.time_ms = 0;
    producer->production.time_left_ms = 0;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
}

static void dc_advance_production_queue(mobj_t *producer) {
    if (!producer) return;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    producer->production.queue_count--;
    if (producer->production.queue_count > 0) {
        producer->production.time_left_ms = producer->production.time_ms;
    } else {
        dc_clear_production(producer);
    }
}

static bool dc_position_available_for_spawn(const level_t *map, const mobj_t *units,
                                            int unit_count, float gx, float gy,
                                            float radius) {
    if (!map || !units) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }
    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(map, x, y)) return false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *other = &units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(other->core.position),
                                   (fvec2_t){ gx, gy }) <
            min_dist * min_dist) return false;
    }
    return true;
}

static bool dc_position_walkable_for_spawn(const level_t *map, float gx, float gy,
                                           float radius) {
    if (!map) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }
    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(map, x, y)) return false;
        }
    }
    return true;
}

static bool dc_find_spawn_position_near(const level_t *map, const mobj_t *units,
                                        int unit_count, const mobj_t *producer,
                                        float radius, float *out_gx,
                                        float *out_gy) {
    if (!map || !units || !producer || !out_gx || !out_gy) return false;
    fvec2_t producer_position = fixedvec3_xy_to_fvec2(producer->core.position);
    int origin_x = (int)floorf(producer_position.x);
    int origin_y = (int)floorf(producer_position.y);
    static const int preferred[][2] = {
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
        { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
    };
    int preferred_count = (int)(sizeof(preferred) / sizeof(preferred[0]));
    for (int dist = 1; dist <= 8; ++dist) {
        for (int i = 0; i < preferred_count; ++i) {
            int x = origin_x + preferred[i][0] * dist;
            int y = origin_y + preferred[i][1] * dist;
            float gx = (float)x + 0.5f;
            float gy = (float)y + 0.5f;
            if (!dc_position_available_for_spawn(map, units, unit_count, gx, gy, radius)) continue;
            *out_gx = gx;
            *out_gy = gy;
            return true;
        }
        for (int dy = -dist; dy <= dist; ++dy) {
            for (int dx = -dist; dx <= dist; ++dx) {
                if (dx != -dist && dx != dist && dy != -dist && dy != dist) continue;
                float gx = (float)(origin_x + dx) + 0.5f;
                float gy = (float)(origin_y + dy) + 0.5f;
                if (!dc_position_available_for_spawn(map, units, unit_count, gx, gy, radius)) continue;
                *out_gx = gx;
                *out_gy = gy;
                return true;
            }
        }
    }
    return false;
}

static bool dc_barracks_release_spawn_point(const gameinfo_t *game_info,
                                            const mobj_t *producer,
                                            const mobj_t *new_unit,
                                            float *out_gx,
                                            float *out_gy) {
    if (!game_info || !producer || !new_unit || !out_gx || !out_gy) return false;
    const state_t *stand = dc_state_at(game_info, new_unit->core.state_id);
    if (!stand) return false;
    int stand_x = 0;
    int stand_y = 0;
    int release_state_id = dc_find_state_by_group_frame(game_info,
                                                        CLIENT_PRODUCTION_BUILD_GROUP,
                                                        CLIENT_TRSCBUILD_FIRST_FRAME);
    int release_x = 0;
    int release_y = 0;
    bool saw_release_trooper = false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        const state_t *state = dc_state_at(game_info, release_state_id);
        if (!state || state->misc1 != CLIENT_PRODUCTION_BUILD_GROUP) break;
        int x = 0;
        int y = 0;
        if (state->sprite == stand->sprite && state->frame == stand->frame) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        int next = state->nextstate;
        if (next == game_info->null_state || next == release_state_id) break;
        release_state_id = next;
    }
    if (!saw_release_trooper) return false;

    fvec2_t producer_position = fixedvec3_xy_to_fvec2(producer->core.position);
    *out_gx = producer_position.x + (float)(release_x - stand_x) / (float)CELL_W;
    *out_gy = producer_position.y - (float)(release_y - stand_y) / (float)CELL_H;
    return true;
}

static void dc_order_barracks_exit_spacing(const level_t *map, mobj_t *units, int unit_count,
                                           int spawned_index, const mobj_t *producer,
                                           float exit_gx, float exit_gy) {
    if (!map || !units || !producer || spawned_index < 0 || spawned_index >= unit_count)
        return;
    bool saved[MAXMOBJS];
    for (int i = 0; i < unit_count; ++i) {
        saved[i] = units[i].selected;
        units[i].selected = false;
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != producer->owner ||
            (unit->traits & MF_MOBILE) == 0) {
            continue;
        }
        if (i == spawned_index ||
            fvec2_distance_squared(fixedvec3_xy_to_fvec2(unit->core.position),
                                   (fvec2_t){ exit_gx, exit_gy }) <= crowd_radius_sq) {
            unit->selected = true;
        }
    }

    fvec2_t delta = fvec2_sub((fvec2_t){ exit_gx, exit_gy },
                             fixedvec3_xy_to_fvec2(producer->core.position));
    float len = sqrtf(fvec2_length_squared(delta));
    if (len < 0.01f) {
        delta = (fvec2_t){ 0.0f, -1.0f };
        len = 1.0f;
    }
    fvec2_t goal = fvec2_add((fvec2_t){ exit_gx, exit_gy },
                            fvec2_scale(delta, 1.5f / len));
    P_MoveOrderAt(map, units, unit_count, goal);

    for (int i = 0; i < unit_count; ++i) {
        units[i].selected = saved[i];
    }
}

static bool dc_spawn_finished_unit_product(const level_t *map,
                                           mobj_t *units, int *unit_count,
                                           int producer_index,
                                           uint16_t actor_id) {
    if (!map || !units || !unit_count || producer_index < 0 ||
        producer_index >= *unit_count || *unit_count >= MAXMOBJS || actor_id == 0) {
        return false;
    }
    const actortype_t *type = NULL;
    const actortype_t *types = (const actortype_t *)mobjinfo;
    for (int i = 0; types && i < num_mobjinfo; ++i) {
        if (types[i].id == actor_id) {
            type = &types[i];
            break;
        }
    }
    if (!type) return false;

    mobj_t new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.core.sprite_id = -1;
    new_unit.attack.target = -1;
    new_unit.harvest.target = -1;
    new_unit.traits = type->traits;
    new_unit.harvest.capacity = type->harvest.capacity;
    new_unit.speed = type->speed;
    new_unit.max_hp = type->max_hp;
    new_unit.hp = type->max_hp;
    new_unit.attack.range = type->attack.range;
    new_unit.attack.damage = type->attack.damage;
    new_unit.attack.cooldown_ms = type->attack.cooldown_ms;
    new_unit.attack.anim_ms = type->attack.anim_ms;
    new_unit.death.anim_ms = type->death.anim_ms;
    new_unit.harvest.state_id = type->harvest.state_id;
    new_unit.muzzle_flash_ms = type->muzzle_flash_ms;
    new_unit.core.render_intensity = 16;
    if (type->sprite_name)
        snprintf(new_unit.core.sprite_name, sizeof(new_unit.core.sprite_name), "%s", type->sprite_name);
    if (type->shadow_name)
        snprintf(new_unit.shadow_name, sizeof(new_unit.shadow_name), "%s", type->shadow_name);
    new_unit.muzzle_flash_sprite = type->muzzle_flash_sprite;
    new_unit.hit_effect_sprite = type->hit_effect_sprite;
    if (type->muzzle_flash_name)
        snprintf(new_unit.muzzle_flash_name, sizeof(new_unit.muzzle_flash_name), "%s", type->muzzle_flash_name);
    if (type->hit_effect_name)
        snprintf(new_unit.hit_effect_name, sizeof(new_unit.hit_effect_name), "%s", type->hit_effect_name);
    new_unit.death_effect_action = type->death_effect_action;
    P_SpawnMobj(gameinfo, &new_unit);

    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;
    float gx = 0.0f;
    float gy = 0.0f;
    mobj_t *producer = &units[producer_index];
    const ProductButton *product = dc_product_by_type(producer->production.product_type);
    bool use_barracks_release = dc_product_uses_barracks_release(producer, product, actor_id);
    if (use_barracks_release &&
        dc_barracks_release_spawn_point(gameinfo, producer, &new_unit, &gx, &gy) &&
        dc_position_walkable_for_spawn(map, gx, gy, radius)) {
        /* The release FIN places the visual handoff; occupied exit cells are cleared below. */
    } else if (!dc_find_spawn_position_near(map, units, *unit_count, producer,
                                            radius, &gx, &gy)) {
        return false;
    }
    new_unit.core.position = fixedvec3_from_fvec2((fvec2_t){ gx, gy }, 0);
    int spawned_index = *unit_count;
    units[(*unit_count)++] = new_unit;
    if (use_barracks_release)
        dc_order_barracks_exit_spacing(map, units, *unit_count, spawned_index, producer, gx, gy);
    return true;
}

bool SB_UpdateProduction(void *sb_ptr, level_t *map,
                         mobj_t *units, int *unit_count,
                         effect_t *effects, int max_effects,
                         float dt) {
    sb_state_t *sb = sb_ptr;
    if (!sb || !sb->active || !map || !units || !unit_count || dt <= 0.0f) return false;
    bool spawned = false;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < *unit_count; ++i) {
        mobj_t *producer = &units[i];
        if (producer->production.queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            producer->production.queue_count = 0;
            dc_clear_production(producer);
            continue;
        }
        if (producer->production.release_active) {
            producer->production.release_time_left_ms -= elapsed_ms;
            if (producer->production.release_time_left_ms > 0) continue;
            uint16_t actor_id = producer->production.actor_id;
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                producer->production.release_time_left_ms = 250;
                continue;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
            continue;
        }
        producer->production.time_left_ms -= elapsed_ms;
        while (producer->production.queue_count > 0 &&
               producer->production.time_left_ms <= 0) {
            uint16_t actor_id = producer->production.actor_id;
            const ProductButton *product =
                dc_product_by_type(producer->production.product_type);
            if (product && dc_start_production_release(map, effects, max_effects,
                                                       producer, product, actor_id)) {
                break;
            }
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                producer->production.time_left_ms = 250;
                break;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
        }
    }
    return spawned;
}

static const char *dc_selected_building_label(const mobj_t *selected) {
    if (!selected) return "";
    switch (selected->type_id) {
    case CLIENT_MT_EXCOPOD: return "Exo-Ctr";
    case CLIENT_MT_BRRKPOD: return "Barracks";
    case CLIENT_MT_ROBOPOD: return "Robo-Ftr";
    case CLIENT_MT_ROBOPOD2: return "Robo-Ftr+";
    case CLIENT_MT_SCNCPOD: return "Sci-Pod";
    case CLIENT_MT_SCNCPOD2: return "Sci-Pod+";
    case CLIENT_MT_RSCHPOD: return "Rsch-Bay";
    default: return "";
    }
}

static int dc_sidebar_command_frame(const SidebarCommand *cmd, const mobj_t *selected) {
    if (!cmd) return 0;
    (void)selected;
    return cmd->frame;
}

static const char *dc_sidebar_command_label(const SidebarCommand *cmd,
                                            const mobj_t *selected) {
    if (!cmd) return "";
    (void)selected;
    if (cmd->id == 37) {
        return "Dig";
    }
    return cmd->label;
}

static void dc_stop_selected_units(mobj_t *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        units[i].movement.path_len = 0;
        units[i].movement.path_index = 0;
        units[i].attack.target = -1;
        units[i].harvest.target = -1;
        units[i].harvest.timer_ms = 0;
        units[i].movement.goal = fixedvec3_xy_to_fvec2(units[i].core.position);
        units[i].movement.order_id = 0;
        units[i].movement.order_arrived = false;
        units[i].core.momentum = fixedvec3_zero();
    }
}

static bool dc_SB_responder(const app_t *app, level_t *map,
                            mobj_t *units, int unit_count, const SDL_Event *e) {
    if (!app || !map || !e) return false;
    if (e->type != SDL_MOUSEBUTTONDOWN) return false;
    int rx = 0, ry = 0;
    R_WindowToRenderPt(app, e->button.x, e->button.y, &rx, &ry);
    UiLayout layout = ui_layout(app);
    if (!irect_contains(layout.outer, (ivec2_t){ rx, ry })) return false;
    int selected_index = -1;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) {
            selected_index = i;
            break;
        }
    }
    mobj_t *selected = selected_index >= 0 ? &units[selected_index] : NULL;
    if (dc_selected_unit_is_player_building(selected)) {
        const ProductButton *products[8] = { 0 };
        int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
        if (e->button.button == SDL_BUTTON_LEFT) {
            for (int i = 0; i < product_count; ++i) {
                if (!irect_contains(product_button_rect(app, i),
                                    (ivec2_t){ rx, ry })) continue;
                const ProductButton *product = products[i];
                uint16_t actor_id = G_ModelActorIdForProduct(product);
                if (actor_id == 0 || map->player_resources[0][0] < product->cost) return true;
                if (dc_enqueue_unit_product(selected, product, actor_id)) {
                    map->player_resources[0][0] -= product->cost;
                }
                return true;
            }
        }
        return true;
    }
    if (e->button.button == SDL_BUTTON_LEFT &&
        irect_contains(layout.buttons[0], (ivec2_t){ rx, ry })) {
        dc_stop_selected_units(units, unit_count);
    }
    return true;
}

static void dc_ui_draw_minimap(app_t *app, const level_t *map, const mobj_t *units, int unit_count,
                               irect_t rect) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    dc_ui_fill(app->renderer, rect, (SDL_Color){ 4, 8, 9, 255 });
    irect_t clip = { rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6 };
    if (clip.w <= 0 || clip.h <= 0) return;
    for (int py = 0; py < clip.h; ++py) {
        int screen_y = py * map->height / clip.h;
        int gy = L_ScreenY(map, screen_y);
        for (int px = 0; px < clip.w; ++px) {
            int gx = px * map->width / clip.w;
            uint32_t color = map->cell_colors ? map->cell_colors[L_Index(map, gx, gy)] : 0xff202820u;
            uint8_t r = (uint8_t)(color >> 16);
            uint8_t g = (uint8_t)(color >> 8);
            uint8_t b = (uint8_t)color;
            SDL_SetRenderDrawColor(app->renderer, r / 2, g / 2, b / 2, 255);
            SDL_RenderDrawPoint(app->renderer, clip.x + px, clip.y + py);
        }
    }
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        int x = clip.x + vent->cell.x * clip.w / map->width;
        int y = clip.y + (int)(L_ScreenY(map, vent->cell.y) * clip.h / map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(app->renderer, dot, vent->active ?
                   (SDL_Color){ 89, 226, 184, 255 } : (SDL_Color){ 68, 86, 84, 255 });
    }
    for (int i = 0; i < unit_count; ++i) {
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        if (units[i].hidden || units[i].remove || position.x < 0.0f || position.y < 0.0f) continue;
        int x = clip.x + (int)(position.x * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(L_ScreenYF(map, position.y) *
                                (float)clip.h / (float)map->height);
        irect_t dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(app->renderer, dot, units[i].owner == 0 ?
                   (SDL_Color){ 218, 214, 135, 255 } : (SDL_Color){ 204, 68, 72, 255 });
    }
    int world_right = app->win.w - 124;
    cell_t tl = R_ScreenToGrid(app, 0, 0);
    cell_t br = R_ScreenToGrid(app, world_right, app->win.h);
    int vx = clip.x + tl.x * clip.w / map->width;
    int vy = clip.y + tl.y * clip.h / map->height;
    int vw = (br.x - tl.x) * clip.w / map->width;
    int vh = (br.y - tl.y) * clip.h / map->height;
    if (vw < 3) vw = 3;
    if (vh < 3) vh = 3;
    irect_t view = { vx, vy, vw, vh };
    dc_ui_stroke(app->renderer, view, (SDL_Color){ 164, 236, 203, 220 });
    dc_ui_stroke(app->renderer, rect, (SDL_Color){ 72, 91, 88, 255 });
}

static void dc_ui_draw_text_right(SDL_Renderer *renderer, const bitmapfont_t *font,
                                  irect_t rect, int y, const char *text,
                                  SDL_Color color) {
    if (!renderer || !font || !text) return;
    int x = rect.x + rect.w - 3 - HU_TextWidth(font, text, 1);
    if (x < rect.x + 2) x = rect.x + 2;
    HU_DrawText(renderer, font, x, y, text, color, 1);
}

static void dc_ui_draw_status(app_t *app, const level_t *map,
                              const bitmapfont_t *font,
                              const UiLayout *layout,
                              const spritecache_t *cache,
                              uint64_t clock) {
    if (!app || !map || !font || !layout) return;
    char text[32];
    const spritesheet_t *buttons = R_CacheLookup(cache, "INTRFACE/MAINBUT.SPR");
    if (buttons && buttons->texture)
        dc_ui_draw_sprite_fit(app->renderer, buttons, 104, layout->money, 0);
    int resources = map->player_resources[0][0];
    if (resources < 0) resources = 0;
    snprintf(text, sizeof(text), "%d", resources);
    dc_ui_draw_text_right(app->renderer, font, layout->money,
                          layout->money.y + 2, text,
                          (SDL_Color){ 41, 217, 230, 255 });

    int days = map->day_rate > 0 ?
        (int)(clock / (uint64_t)map->day_rate / 2u) : 0;
    if (days > 999) days = 999;
    snprintf(text, sizeof(text), "%03d", days);
    int x = layout->days.x - HU_TextWidth(font, text, 1) / 2;
    HU_DrawTextRemapped(app->renderer, font, x, layout->days.y, text,
                        (SDL_Color){ 255, 255, 255, 255 }, 1, 0);
}

static void dc_SB_drawer(app_t *app, const level_t *map,
                         const mobj_t *units, int unit_count,
                         const spritecache_t *cache, const bitmapfont_t *font,
                         const Sidebar *sidebar,
                         const spritesheet_t *background) {
    if (!app || !font || !font->sprite.texture) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    UiLayout layout = ui_layout(app);
    if (background && background->texture) {
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 516, 0, 124, 480 }, layout.outer);
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 0, 455, 516, 25 },
                              ui_rect(app, 0, 455, 516, 25));
    } else {
        dc_ui_fill(app->renderer, layout.outer, (SDL_Color){ 2, 2, 2, 255 });
        dc_ui_fill(app->renderer, ui_rect(app, 0, 455, 640, 25),
                   (SDL_Color){ 3, 3, 3, 255 });
        dc_ui_stroke(app->renderer, layout.outer, (SDL_Color){ 178, 178, 178, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 0, 455, 640, 18),
                     (SDL_Color){ 164, 164, 164, 255 });
        dc_ui_stroke(app->renderer, layout.minimap, (SDL_Color){ 154, 154, 154, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 516, 0, 107, 92),
                     (SDL_Color){ 86, 86, 86, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 516, 92, 124, 363),
                     (SDL_Color){ 154, 154, 154, 255 });

        for (int i = 0; i < 3; ++i) {
            dc_ui_fill(app->renderer, layout.tabs[i], (SDL_Color){ 126, 126, 126, 255 });
            dc_ui_stroke(app->renderer, layout.tabs[i], (SDL_Color){ 38, 38, 38, 255 });
            char tab[2] = { (char)('1' + i), '\0' };
            HU_DrawText(app->renderer, font,
                               layout.tabs[i].x + layout.tabs[i].w / 2 - HU_TextWidth(font, tab, 1) / 2,
                               layout.tabs[i].y + layout.tabs[i].h / 2 - font->line_h / 2,
                               tab, (SDL_Color){ 24, 24, 24, 255 }, 1);
        }
    }

    SDL_Color dim = { 112, 130, 125, 255 };
    SDL_Color amber = { 231, 194, 94, 255 };
    char line[96];

    const spritesheet_t *buttons = R_CacheLookup(cache, "INTRFACE/MAINBUT.SPR");
    irect_t mini = {
        layout.minimap.x + 2,
        layout.minimap.y + 2,
        layout.minimap.w - 4,
        layout.minimap.h - 4,
    };
    dc_ui_draw_minimap(app, map, units, unit_count, mini);

    Sidebar fallback_sidebar;
    if (!sidebar) {
        sidebar_defaults(&fallback_sidebar);
        sidebar = &fallback_sidebar;
    }
    int hover_button = -1;
    const mobj_t *selected = dc_first_selected_unit(units, unit_count);
    const ProductButton *products[8] = { 0 };
    int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
    bool product_mode = dc_selected_unit_is_player_building(selected);
    int visible_button_count = product_mode ? product_count : sidebar->command_count;
    for (int i = 0; i < visible_button_count; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, i) :
            layout.buttons[i];
        if (irect_contains(button_rect, app->mouse)) {
            hover_button = i;
            break;
        }
    }
    if (!background || !background->texture) {
        dc_ui_fill(app->renderer, layout.build, (SDL_Color){ 160, 160, 160, 255 });
        dc_ui_stroke(app->renderer, layout.build, (SDL_Color){ 39, 39, 39, 255 });
        HU_DrawText(app->renderer, font,
                           layout.build.x + layout.build.w / 2 - HU_TextWidth(font, "BUILD", 5) / 2,
                           layout.build.y + layout.build.h / 2 - font->line_h / 2,
                           "BUILD", (SDL_Color){ 24, 24, 24, 255 }, 1);
    }
    if (hover_button >= 0) {
        if (product_mode && products[hover_button]) {
            snprintf(line, sizeof(line), "%s %d",
                     products[hover_button]->label, products[hover_button]->cost);
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line,
                           map->player_resources[0][0] >= products[hover_button]->cost ?
                           amber : (SDL_Color){ 208, 103, 88, 255 },
                           1);
        } else {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           dc_sidebar_command_label(&sidebar->commands[hover_button], selected),
                           amber, 1);
        }
    } else if (product_mode) {
        if (selected && selected->production.queue_count > 0 &&
            selected->production.time_ms > 0) {
            int done = selected->production.time_ms - selected->production.time_left_ms;
            int pct = done * 100 / selected->production.time_ms;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(line, sizeof(line), "Training x%d %d%%",
                     selected->production.queue_count, pct);
        } else {
            snprintf(line, sizeof(line), "%s", dc_selected_building_label(selected));
        }
        if (line[0] != '\0') {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line, dim, 1);
        }
    }
    int button_slots = product_mode ? 8 : 6;
    for (int i = 0; i < button_slots; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, i) :
            layout.buttons[i];
        if (product_mode && i >= product_count) {
            dc_ui_fill(app->renderer, button_rect, (SDL_Color){ 10, 12, 12, 185 });
            dc_ui_stroke(app->renderer, button_rect, (SDL_Color){ 66, 72, 70, 255 });
            continue;
        }
        int frame = product_mode && products[i] ? products[i]->icon_frame :
            dc_sidebar_command_frame(&sidebar->commands[i], selected);
        if (buttons && buttons->texture) {
            dc_ui_draw_sprite_fit(app->renderer, buttons, frame, button_rect, 0);
            if (product_mode && products[i] && map->player_resources[0][0] < products[i]->cost) {
                dc_ui_fill(app->renderer, button_rect, (SDL_Color){ 0, 0, 0, 105 });
            }
        } else {
            SDL_Color fill = (i == 0 && !product_mode) ? (SDL_Color){ 150, 150, 145, 255 } :
                             (SDL_Color){ 175, 175, 168, 255 };
            dc_ui_fill(app->renderer, button_rect, fill);
            dc_ui_stroke(app->renderer, button_rect, i == 0 && !product_mode ?
                         (SDL_Color){ 136, 58, 53, 255 } : (SDL_Color){ 72, 95, 88, 255 });
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

static void render_hud_messages(app_t *app, const hudtext_t *hud, const bitmapfont_t *font) {
    if (!app || !hud || !font || !font->sprite.texture || hud->count <= 0) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    UiLayout layout = ui_layout(app);
    char message[62];
    snprintf(message, sizeof(message), "%.61s", hud->messages[hud->count - 1].text);
    HU_DrawTextRemapped(app->renderer, font, layout.message.x, layout.message.y,
                        message, (SDL_Color){ 255, 255, 255, 255 }, 1, 2);
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

void *SB_Init(app_t *app, const char *data_root) {
    if (!app || !data_root) return NULL;
    sb_state_t *sb = calloc(1, sizeof(sb_state_t));
    if (!sb) return NULL;
    sb->active = true;
    sidebar_defaults(&sb->sidebar);

    sb->font_ready = HU_LoadFont(app->renderer, data_root, &sb->font);
    if (!sb->font_ready)
        fprintf(stderr, "warning: failed to create Dark Colony UI font\n");
    sidebar_load(&sb->sidebar, data_root);

    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/INTRFACE.GIF");
    if (!W_LoadGIFTexture(app->renderer, path, &sb->background))
        fprintf(stderr, "warning: failed to load Dark Colony UI background %s\n", path);
    return sb;
}

bool SB_Responder(void *sb_ptr, const app_t *app, level_t *map,
                  mobj_t *units, int unit_count, const SDL_Event *event) {
    sb_state_t *sb = sb_ptr;
    return sb && sb->active &&
           dc_SB_responder(app, map, units, unit_count, event);
}

void SB_Ticker(void *sb_ptr) {
    sb_state_t *sb = sb_ptr;
    if (sb && sb->active) sb->clock++;
}

void SB_Drawer(void *sb_ptr, app_t *app, const level_t *map,
               const mobj_t *units, int unit_count,
               const spritecache_t *sprites, const hudtext_t *hud) {
    sb_state_t *sb = sb_ptr;
    if (!sb || !sb->active || !sb->font_ready) return;
    dc_SB_drawer(app, map, units, unit_count, sprites, &sb->font,
                 &sb->sidebar, &sb->background);
    UiLayout layout = ui_layout(app);
    dc_ui_draw_status(app, map, &sb->font, &layout, sprites, sb->clock);
    render_hud_messages(app, hud, &sb->font);
}

void SB_Shutdown(void *sb_ptr) {
    sb_state_t *sb = sb_ptr;
    if (!sb) return;
    R_FreeSprite(&sb->background);
    HU_FreeFont(&sb->font);
    free(sb);
}
