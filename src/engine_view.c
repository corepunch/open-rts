#define _DEFAULT_SOURCE
#include "engine_internal.h"

static SDL_Rect sprite_visible_bounds(const SpriteSheet *sprite, int frame);

static int app_cell_w(const App *app) {
    int scale = app->render_scale > 0 ? app->render_scale : 1;
    return (app->cell_w > 0 ? app->cell_w : CELL_W) * scale;
}

static int app_cell_h(const App *app) {
    int scale = app->render_scale > 0 ? app->render_scale : 1;
    return (app->cell_h > 0 ? app->cell_h : CELL_H) * scale;
}

static int app_scale(const App *app) {
    return app->render_scale > 0 ? app->render_scale : 1;
}

static float viewport_scale_x(const App *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_w / (float)window_w;
}

static float viewport_scale_y(const App *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_h / (float)window_h;
}

static int app_tile_w(const App *app, const Tileset *tileset) {
    return tileset->tile_w * app_scale(app);
}

static int app_tile_h(const App *app, const Tileset *tileset) {
    return tileset->tile_h * app_scale(app);
}

static int tileset_animate_value(const Tileset *tileset, int value, uint32_t ticks_ms) {
    if (!tileset->animations || tileset->animation_count <= 0) return value;
    for (int i = 0; i < tileset->animation_count; ++i) {
        const TileAnimation *anim = &tileset->animations[i];
        if (anim->value != value) continue;
        int frame = (int)((ticks_ms / anim->frame_ms) % (uint32_t)anim->frame_count);
        return anim->frames[frame];
    }
    return value;
}

static int tileset_resolve_tile(const Tileset *tileset, int value, uint32_t ticks_ms) {
    value = tileset_animate_value(tileset, value, ticks_ms);
    if (value < 0 || tileset->count <= 0) return -1;
    if (tileset->tile_lookup) {
        if (value < tileset->tile_lookup_count) {
            int tile = tileset->tile_lookup[value];
            if (tile >= 0 && tile < tileset->count) return tile;
        }
        return -1;
    }
    if (value < tileset->count) return value;
    return value % tileset->count;
}


void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy) {
    *sx = gx * (float)app_cell_w(app) + app->cam_x;
    *sy = gy * (float)app_cell_h(app) + app->cam_y;
}

static void screen_to_grid_point(const App *app, int sx, int sy, float *gx, float *gy) {
    if (gx) *gx = ((float)sx - app->cam_x) / (float)app_cell_w(app);
    if (gy) *gy = ((float)sy - app->cam_y) / (float)app_cell_h(app);
}

Cell screen_to_grid(const App *app, int sx, int sy) {
    return (Cell){ (int)floorf(((float)sx - app->cam_x) / (float)app_cell_w(app)),
                   (int)floorf(((float)sy - app->cam_y) / (float)app_cell_h(app)) };
}

void refresh_app_viewport(App *app) {
    if (!app || !app->window || !app->renderer) return;
    int render_w = 0, render_h = 0;
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        render_w <= 0 || render_h <= 0) {
        SDL_GetWindowSize(app->window, &render_w, &render_h);
    }
    if (render_w > 0) app->win_w = render_w;
    if (render_h > 0) app->win_h = render_h;
}

void window_to_render_point(const App *app, int wx, int wy, int *rx, int *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (int)lroundf((float)wx * sx);
    if (ry) *ry = (int)lroundf((float)wy * sy);
}

void window_to_render_delta(const App *app, int wx, int wy, float *rx, float *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (float)wx * sx;
    if (ry) *ry = (float)wy * sy;
}

void render_grid_cell(App *app, int gx, int gy, SDL_Color color) {
    float sx, sy;
    grid_to_screen(app, (float)gx, (float)gy, &sx, &sy);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_RenderDrawRect(app->renderer, &r);
}

static void render_blocked_overlay(App *app, const GameMap *map) {
    if (!app || !map || !map->blocked) return;
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (!map->blocked[map_index(map, x, y)]) continue;
            float sx, sy;
            grid_to_screen(app, (float)x, (float)y, &sx, &sy);
            if (sx < -cell_w || sy < -cell_h ||
                sx > app->win_w + cell_w || sy > app->win_h + cell_h) {
                continue;
            }
            SDL_Rect r = { (int)sx, (int)sy, cell_w, cell_h };
            SDL_SetRenderDrawColor(app->renderer, 230, 45, 40, 92);
            SDL_RenderFillRect(app->renderer, &r);
            SDL_SetRenderDrawColor(app->renderer, 255, 205, 64, 180);
            SDL_RenderDrawRect(app->renderer, &r);
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    SDL_Rect src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
}

static void render_tile_at_flipped(App *app, const Tileset *tileset, int tile,
                                   SDL_Rect src_part, SDL_Rect dst_part, uint8_t flip_flags) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    SDL_Rect src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RendererFlip flip = (flip_flags & 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    if (flip == SDL_FLIP_NONE) {
        SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
    } else {
        SDL_RenderCopyEx(app->renderer, tileset->texture, &src, &dst_part, 0.0, NULL, flip);
    }
}

void render_map(App *app, const GameMap *map, const Tileset *tileset) {
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset * app_scale(app);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            float sx, sy;
            grid_to_screen(app, (float)x, (float)y, &sx, &sy);
            if ((map->render_features & MAP_RENDER_USE_CELL_COLORS) && map->cell_colors) {
                if (sx < -cell_w || sy < -cell_h ||
                    sx > app->win_w + cell_w || sy > app->win_h + cell_h) {
                    continue;
                }
                uint32_t color = map->cell_colors[map_index(map, x, y)];
                SDL_SetRenderDrawColor(app->renderer,
                                       (uint8_t)(color >> 16),
                                       (uint8_t)(color >> 8),
                                       (uint8_t)color,
                                       255);
                SDL_Rect dst = { (int)sx, (int)sy, cell_w, cell_h };
                SDL_RenderFillRect(app->renderer, &dst);
                continue;
            }
            if (sx < -tile_w || sy < -tile_h ||
                sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                continue;
            }
            int idx = map_index(map, x, y);
            int tile = map->tile_ids[idx];
            if ((map->render_features & MAP_RENDER_SKIP_ZERO_TILES) && tile == 0) continue;
            SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
            SDL_Rect dst = {
                (int)sx,
                (int)(sy + draw_y_offset),
                tile_w,
                tile_h,
            };
            uint8_t base_flip = map->tile_flip_flags[0] ? map->tile_flip_flags[0][idx] : 0;
            render_tile_at_flipped(app, tileset, tile, src, dst, base_flip);
            if ((map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) == 0) {
                for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
                    if (!map->tile_overlays[layer]) continue;
                    int overlay = map->tile_overlays[layer][idx];
                    if (overlay <= 0) continue;
                    uint8_t overlay_flip = map->tile_flip_flags[layer + 1] ?
                        map->tile_flip_flags[layer + 1][idx] : 0;
                    render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
                }
            }
        }
    }

    for (int layer = 0;
         !(map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) &&
         layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS;
         ++layer) {
        if (!map->tile_overlays[layer]) continue;
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                int idx = map_index(map, x, y);
                int overlay = map->tile_overlays[layer][idx];
                if (overlay <= 0) continue;
                SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
                SDL_Rect dst = {
                    (int)sx,
                    (int)(sy + draw_y_offset),
                    tile_w,
                    tile_h,
                };
                uint8_t overlay_flip = map->tile_flip_flags[layer + 1] ?
                    map->tile_flip_flags[layer + 1][idx] : 0;
                render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
            }
        }
    }

    if ((map->render_features & MAP_RENDER_SMOOTH_TRANSITIONS) &&
        !(map->render_features & MAP_RENDER_USE_CELL_COLORS) &&
        map->render_transitions) {
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                int dx = (int)sx;
                int dy = (int)(sy + draw_y_offset);
                map->render_transitions(app, map, tileset, x, y, dx, dy);
            }
        }
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_NONE);
    }

    if (app->show_blocked) {
        render_blocked_overlay(app, map);
    }

    if (app->show_grid) {
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                render_grid_cell(app, x, y, (SDL_Color){ 50, 78, 72, 80 });
            }
        }
    }
}

const SpriteSheet *sprite_cache_lookup(const SpriteCache *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i].sprite;
    }
    return NULL;
}

CachedSprite *sprite_cache_find(SpriteCache *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i];
    }
    return NULL;
}

static const SpriteSequence *sprite_sequence_find(const SpriteSheet *sprite, const char *name) {
    if (!sprite || !name) return NULL;
    for (int i = 0; i < sprite->sequence_count; ++i) {
        if (strcmp(sprite->sequences[i].name, name) == 0) return &sprite->sequences[i];
    }
    return NULL;
}

static int sequence_facing_index(const SpriteSequence *seq, int direction_code) {
    if (!seq || seq->facings <= 0) return 0;
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < seq->facings && i < MAX_SEQUENCE_FACINGS; ++i) {
        int code = seq->direction_codes[i];
        int delta = abs(code - direction_code);
        if (delta > 8) delta = 16 - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static int sprite_sequence_frame(const SpriteSheet *sprite, const char *sequence_name,
                                 int facing_code, int sequence_frame) {
    const SpriteSequence *seq = sprite_sequence_find(sprite, sequence_name);
    if (!seq || seq->facings <= 0 || seq->length <= 0) {
        rts_debug_effects_log("sequence miss sprite_frames=%d sequence=%s facing=%d anim=%d",
                          sprite ? sprite->frame_count : 0,
                          sequence_name ? sequence_name : "(null)",
                          facing_code, sequence_frame);
        return -1;
    }
    int facing = sequence_facing_index(seq, facing_code);
    int anim = sequence_frame < 0 ? seq->length - 1 : sequence_frame;
    if (anim >= seq->length) anim = seq->length - 1;
    int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
    int start = seq->frame_starts[facing];
    if (start < 0 || start >= sprite->frame_count) {
        rts_debug_effects_log("sequence bad start sequence=%s facing=%d/%d code=%d start=%d frame_count=%d",
                          sequence_name, facing, seq->facings, facing_code, start,
                          sprite ? sprite->frame_count : 0);
        return -1;
    }
    int frame = start + anim * frame_stride;
    if (frame < 0) return start;
    if (frame >= sprite->frame_count) return sprite->frame_count - 1;
    return frame;
}


static void render_decoration_sprite(App *app, const MapDecoration *dec, const SpriteSheet *sprite) {
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    grid_to_screen(app, (float)dec->gx, (float)dec->gy, &sx, &sy);
    int footprint_w = dec->footprint_w > 0 ? dec->footprint_w : 1;
    int footprint_h = dec->footprint_h > 0 ? dec->footprint_h : 1;
    int scale = app_scale(app);
    int sprite_w = sprite->frame_w * scale;
    int sprite_h = sprite->frame_h * scale;

    int frame = -1;
    if (dec->sequence_name[0] != '\0') {
        frame = sprite_sequence_frame(sprite, dec->sequence_name, dec->facing_code,
                                      dec->frame_index);
    }
    if (frame < 0) frame = dec->frame_index >= 0 && dec->frame_index < sprite->frame_count ?
        dec->frame_index : 0;

    SDL_Rect dst;
    if (dec->center_anchor) {
        grid_to_screen(app, (float)dec->gx + 0.5f, (float)dec->gy + 0.5f, &sx, &sy);
        SDL_Rect bounds = sprite_visible_bounds(sprite, frame);
        float ground_offset_y = ((float)bounds.y + (float)bounds.h) * (float)scale;
        dst = (SDL_Rect){
            (int)(sx - sprite_w / 2),
            (int)(sy - ground_offset_y),
            sprite_w,
            sprite_h,
        };
    } else {
        dst = (SDL_Rect){
            (int)(sx + (float)(footprint_w * app_cell_w(app) - sprite_w) * 0.5f),
            (int)(sy + (float)(footprint_h * app_cell_h(app) - sprite_h)),
            sprite_w,
            sprite_h,
        };
    }
    if (dst.x > app->win_w || dst.y > app->win_h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        return;
    }
    SDL_RendererFlip flip = (dec->render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
}

void render_decorations(App *app, const GameMap *map, const SpriteCache *cache) {
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->shadow_name));
        render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->sprite_name));
    }
}

static SDL_Rect normalized_rect(int x0, int y0, int x1, int y1) {
    SDL_Rect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = abs(x1 - x0);
    r.h = abs(y1 - y0);
    return r;
}

static bool point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h;
}

static float unit_pick_radius_px(const App *app, const Unit *unit) {
    float cell = ((float)app_cell_w(app) + (float)app_cell_h(app)) * 0.5f;
    float radius = rts_unit_radius_cells(unit) * cell;
    float min_radius = 12.0f * (float)app_scale(app);
    return radius < min_radius ? min_radius : radius;
}

static bool circle_intersects_rect(float cx, float cy, float radius, SDL_Rect r) {
    float nearest_x = cx;
    float nearest_y = cy;
    if (nearest_x < (float)r.x) nearest_x = (float)r.x;
    if (nearest_x > (float)(r.x + r.w)) nearest_x = (float)(r.x + r.w);
    if (nearest_y < (float)r.y) nearest_y = (float)r.y;
    if (nearest_y > (float)(r.y + r.h)) nearest_y = (float)(r.y + r.h);
    float dx = cx - nearest_x;
    float dy = cy - nearest_y;
    return dx * dx + dy * dy <= radius * radius;
}

static int pick_unit_at(const App *app, const Unit *units, int unit_count, int x, int y,
                        int owner_filter) {
    int best = -1;
    float best_score = 1000000000.0f;
    for (int i = unit_count - 1; i >= 0; --i) {
        const Unit *unit = &units[i];
        if (unit->hp <= 0 || (unit->traits & RTS_TRAIT_SELECTABLE) == 0) continue;
        if (owner_filter >= 0 && unit->owner != owner_filter) continue;
        float sx = 0.0f, sy = 0.0f;
        grid_to_screen(app, unit->gx, unit->gy, &sx, &sy);
        float radius = unit_pick_radius_px(app, unit);
        float dx = (float)x - sx;
        float dy = (float)y - sy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 > radius * radius || dist2 >= best_score) continue;
        best_score = dist2;
        best = i;
    }
    return best;
}


static int sprite_frame_for_unit(const SpriteSheet *sprite, const Unit *unit, uint32_t ticks) {
    bool moving = unit->path_index > 0 && unit->path_index < unit->path_len;
    bool attacking = unit->attack_anim_left_ms > 0;
    bool dead = unit->hp <= 0;
    const SpriteSequence *seq = NULL;
    bool using_death_sequence = false;
    if (dead) {
        seq = sprite_sequence_find(sprite, "die");
        if (seq) using_death_sequence = true;
        if (!seq) {
            seq = sprite_sequence_find(sprite, "death");
            if (seq) using_death_sequence = true;
        }
        if (!seq) seq = sprite_sequence_find(sprite, "shoot");
    } else {
        seq = sprite_sequence_find(sprite, attacking ? "shoot" : (moving ? "run" : "stand"));
        if (!seq && attacking) seq = sprite_sequence_find(sprite, "attack");
        if (!seq && moving) seq = sprite_sequence_find(sprite, "walk");
    }
    if (!seq) seq = sprite_sequence_find(sprite, "idle");
    if (!seq && dead) seq = sprite_sequence_find(sprite, "stand");
    if (seq && seq->facings > 0 && seq->length > 0) {
        int direction_code = unit->facing_code;
        if (moving && !dead) {
            Cell c = unit->path[unit->path_index];
            bool final = unit->path_index == unit->path_len - 1;
            float tx = final ? unit->move_goal_gx : (float)c.x + 0.5f;
            float ty = final ? unit->move_goal_gy : (float)c.y + 0.5f;
            float dx = tx - unit->gx;
            float dy = ty - unit->gy;
            direction_code = rts_direction_code_from_vector(NULL, dx, dy);
        }
        int facing = sequence_facing_index(seq, direction_code);
        int tick_ms = seq->tick_ms > 0 ? seq->tick_ms : 120;
        int anim = 0;
        if (dead && using_death_sequence && unit->death_started && seq->length > 1) {
            int elapsed_ms = unit->death_anim_ms - unit->death_anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (dead && seq->length > 1) {
            anim = seq->length - 1;
        } else if (attacking && seq->length > 1) {
            int elapsed_ms = unit->attack_anim_ms - unit->attack_anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (moving && seq->length > 1) {
            anim = (int)((ticks / (uint32_t)tick_ms) % (uint32_t)seq->length);
        }
        int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
        int frame = seq->frame_starts[facing] + anim * frame_stride;
        if (frame >= 0 && frame < sprite->frame_count) return frame;
    }

    int anim_frames = sprite->primary_frames_per_rotation > 0 ?
        sprite->primary_frames_per_rotation : sprite->frame_count;
    if (moving && anim_frames > 0 && sprite->sequence_count == 0) {
        return (int)((ticks / 120) % (uint32_t)anim_frames);
    }
    return 0;
}

static SDL_Rect sprite_visible_bounds(const SpriteSheet *sprite, int frame) {
    if (sprite && sprite->frame_bounds && frame >= 0 && frame < sprite->frame_count) {
        SDL_Rect r = sprite->frame_bounds[frame];
        if (r.w > 0 && r.h > 0) return r;
    }
    return (SDL_Rect){ 0, 0, sprite ? sprite->frame_w : 1, sprite ? sprite->frame_h : 1 };
}

static void draw_selection_ellipse(App *app, float cx, float cy, float rx, float ry,
                                   SDL_Color color) {
    if (!app || !app->renderer || rx <= 0.0f || ry <= 0.0f) return;
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    const int segments = 40;
    float prev_x = cx + rx;
    float prev_y = cy;
    for (int i = 1; i <= segments; ++i) {
        float a = ((float)i / (float)segments) * 6.283185307179586f;
        float x = cx + cosf(a) * rx;
        float y = cy + sinf(a) * ry;
        SDL_RenderDrawLine(app->renderer, (int)lroundf(prev_x), (int)lroundf(prev_y),
                           (int)lroundf(x), (int)lroundf(y));
        prev_x = x;
        prev_y = y;
    }
}

static void render_unit_sprite(App *app, const Unit *u, const SpriteSheet *fallback_sprite,
                               const SpriteCache *cache, const RtsGameInfo *game_info,
                               uint32_t ticks) {
    if (!u || (u->traits & RTS_TRAIT_RENDERABLE) == 0) return;
    const char *sprite_name = u->sprite_name;
    if (game_info && u->sprite_id >= 0 && u->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[u->sprite_id]) {
        sprite_name = game_info->sprnames[u->sprite_id];
    }
    const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
    if (!sprite) sprite = fallback_sprite;
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    grid_to_screen(app, u->gx, u->gy, &sx, &sy);
    int frame = game_info ? u->frame : sprite_frame_for_unit(sprite, u, ticks);
    uint32_t render_flags = game_info ? u->render_flags : 0;
    if (frame >= sprite->frame_count) frame = 0;
    if (frame < 0) frame = 0;
    SDL_Rect bounds = sprite_visible_bounds(sprite, frame);
    int scale = app_scale(app);
    int sprite_w = sprite->frame_w * scale;
    int sprite_h = sprite->frame_h * scale;
    float content_w = (float)bounds.w * (float)scale;
    float rx = rts_unit_radius_cells(u) * (float)app_cell_w(app);
    float min_rx = content_w * 0.34f;
    if (rx < min_rx) rx = min_rx;
    float ry = rx * 0.38f;
    float ground_offset_y = ((float)bounds.y + (float)bounds.h) * (float)scale - ry * 0.35f;
    SDL_Rect dst = {
        (int)(sx - sprite_w / 2),
        (int)(sy - ground_offset_y),
        sprite_w,
        sprite_h,
    };
    const SpriteSheet *shadow = sprite_cache_lookup(cache, u->shadow_name);
    if (shadow && shadow->texture && shadow->frame_count > 0) {
        int shadow_frame = frame < shadow->frame_count ? frame : 0;
        int shadow_w = shadow->frame_w * scale;
        int shadow_h = shadow->frame_h * scale;
        SDL_Rect shadow_dst = {
            (int)(sx - shadow_w / 2),
            (int)(sy - shadow_h / 2),
            shadow_w,
            shadow_h,
        };
        SDL_RenderCopy(app->renderer, shadow->texture, &shadow->frames[shadow_frame], &shadow_dst);
    }
    float content_y = (float)dst.y + (float)bounds.y * (float)scale;
    if (u->selected && (u->traits & RTS_TRAIT_SELECTABLE) != 0) {
        draw_selection_ellipse(app, sx, sy, rx + 2.0f, ry + 1.0f, (SDL_Color){ 15, 35, 30, 180 });
        draw_selection_ellipse(app, sx, sy, rx, ry, (SDL_Color){ 98, 224, 161, 255 });
    }
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp) {
        int bar_w = sprite_w / 2;
        int bar_h = app_scale(app) < 2 ? 2 : 3;
        int bx = (int)(sx - bar_w / 2);
        int by = (int)content_y - bar_h - 4;
        SDL_Rect back = { bx, by, bar_w, bar_h };
        SDL_Rect fill = { bx, by, (bar_w * u->hp) / u->max_hp, bar_h };
        SDL_SetRenderDrawColor(app->renderer, 40, 20, 20, 220);
        SDL_RenderFillRect(app->renderer, &back);
        SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 230);
        SDL_RenderFillRect(app->renderer, &fill);
    }
}

void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        render_unit_sprite(app, &units[i], fallback_sprite, cache, game_info, ticks);
    }
}

typedef enum {
    RENDER_ITEM_OVERLAY,
    RENDER_ITEM_DECORATION,
    RENDER_ITEM_UNIT,
} RenderItemKind;

typedef struct {
    RenderItemKind kind;
    float sort_y;
    int index;
    int x;
    int y;
    int layer;
} RenderItem;

static int compare_render_items(const void *a, const void *b) {
    const RenderItem *ia = a;
    const RenderItem *ib = b;
    if (ia->sort_y < ib->sort_y) return -1;
    if (ia->sort_y > ib->sort_y) return 1;
    if (ia->kind != ib->kind) return (int)ia->kind - (int)ib->kind;
    return ia->index - ib->index;
}

static void render_overlay_tile_item(App *app, const GameMap *map, const Tileset *tileset,
                                     int x, int y, int layer) {
    if (!app || !map || !tileset || layer < 0 || layer >= map->tile_overlay_count ||
        layer >= MAX_TILE_OVERLAYS || !map->tile_overlays[layer]) {
        return;
    }
    int idx = map_index(map, x, y);
    int overlay = map->tile_overlays[layer][idx];
    if (overlay <= 0) return;

    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset * app_scale(app);
    float sx, sy;
    grid_to_screen(app, (float)x, (float)y, &sx, &sy);
    if (sx < -tile_w || sy < -tile_h ||
        sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
        return;
    }
    SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
    SDL_Rect dst = {
        (int)sx,
        (int)(sy + draw_y_offset),
        tile_w,
        tile_h,
    };
    uint8_t overlay_flip = map->tile_flip_flags[layer + 1] ?
        map->tile_flip_flags[layer + 1][idx] : 0;
    render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
}

void render_world_objects(App *app, const GameMap *map, const Tileset *tileset,
                          const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                          const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks) {
    if (!app || !map) return;
    int overlay_count = 0;
    if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][map_index(map, x, y)] > 0) overlay_count++;
                }
            }
        }
    }

    int decoration_count = map->decoration_count > 0 ? map->decoration_count : 0;
    int total = overlay_count + decoration_count + (unit_count > 0 ? unit_count : 0);
    if (total <= 0) return;
    RenderItem *items = malloc((size_t)total * sizeof(*items));
    if (!items) {
        render_decorations(app, map, cache);
        render_units(app, units, unit_count, fallback_sprite, cache, game_info, ticks);
        return;
    }

    int count = 0;
    if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][map_index(map, x, y)] <= 0) continue;
                    items[count++] = (RenderItem){
                        .kind = RENDER_ITEM_OVERLAY,
                        .sort_y = (float)y + 1.0f + (float)layer * 0.001f,
                        .index = map_index(map, x, y),
                        .x = x,
                        .y = y,
                        .layer = layer,
                    };
                }
            }
        }
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        float sort_y = dec->center_anchor ?
            (float)dec->gy + 0.5f :
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1);
        items[count++] = (RenderItem){
            .kind = RENDER_ITEM_DECORATION,
            .sort_y = sort_y,
            .index = i,
        };
    }
    for (int i = 0; i < unit_count; ++i) {
        items[count++] = (RenderItem){
            .kind = RENDER_ITEM_UNIT,
            .sort_y = units[i].gy,
            .index = i,
        };
    }

    qsort(items, (size_t)count, sizeof(*items), compare_render_items);
    for (int i = 0; i < count; ++i) {
        RenderItem *item = &items[i];
        if (item->kind == RENDER_ITEM_OVERLAY) {
            render_overlay_tile_item(app, map, tileset, item->x, item->y, item->layer);
        } else if (item->kind == RENDER_ITEM_DECORATION) {
            const MapDecoration *dec = &map->decorations[item->index];
            render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->shadow_name));
            render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->sprite_name));
        } else {
            render_unit_sprite(app, &units[item->index], fallback_sprite, cache, game_info, ticks);
        }
    }
    free(items);
}

static int sprite_frame_for_effect(const SpriteSheet *sprite, const RtsVisualEffect *effect) {
    if (!sprite || !effect) return 0;
    int frame_ms = effect->frame_ms > 0 ? effect->frame_ms : 90;
    int anim = effect->age_ms / frame_ms;
    if (effect->sequence_name[0] != '\0') {
        int frame = sprite_sequence_frame(sprite, effect->sequence_name, effect->facing_code, anim);
        if (frame >= 0) return frame;
        if (strcmp(effect->sequence_name, "die") == 0) {
            frame = sprite_sequence_frame(sprite, "death", effect->facing_code, anim);
            if (frame >= 0) return frame;
        }
    }
    if (sprite->frame_count <= 0) return 0;
    if (anim >= sprite->frame_count) anim = sprite->frame_count - 1;
    return anim < 0 ? 0 : anim;
}

void render_visual_effects(App *app, const RtsVisualEffect *effects, int max_effects,
                           const SpriteCache *cache, const RtsGameInfo *game_info) {
    if (!effects || max_effects <= 0) return;
    for (int i = 0; i < max_effects; ++i) {
        const RtsVisualEffect *effect = &effects[i];
        if (!effect->active) continue;
        const char *sprite_name = effect->sprite_name;
        if (effect->use_state && game_info && effect->sprite_id >= 0 &&
            effect->sprite_id < game_info->sprite_count &&
            game_info->sprnames && game_info->sprnames[effect->sprite_id]) {
            sprite_name = game_info->sprnames[effect->sprite_id];
        }
        const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
        if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
            rts_debug_effects_log("render skip slot=%d sprite=%s sequence=%s reason=missing-cache",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)");
            continue;
        }

        float sx, sy;
        grid_to_screen(app, effect->gx, effect->gy, &sx, &sy);
        int scale = app_scale(app);
        int sprite_w = sprite->frame_w * scale;
        int sprite_h = sprite->frame_h * scale;
        SDL_Rect dst = {
            (int)(sx - sprite_w / 2),
            (int)(sy - sprite_h / 2),
            sprite_w,
            sprite_h,
        };
        if (dst.x > app->win_w || dst.y > app->win_h ||
            dst.x + dst.w < 0 || dst.y + dst.h < 0) {
            rts_debug_effects_log("render skip slot=%d sprite=%s sequence=%s frame_count=%d pos=%.2f,%.2f dst=%d,%d,%d,%d reason=offscreen",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)",
                              sprite->frame_count, effect->gx, effect->gy,
                              dst.x, dst.y, dst.w, dst.h);
            continue;
        }
        int frame = effect->use_state ? effect->frame : sprite_frame_for_effect(sprite, effect);
        if (frame < 0 || frame >= sprite->frame_count) frame = 0;
        if (effect->screen_offset_x != 0 || effect->screen_offset_y != 0) {
            dst.x += effect->screen_offset_x * scale;
            dst.y += effect->screen_offset_y * scale;
        }
        rts_debug_effects_log("render slot=%d sprite=%s sequence=%s age=%d/%d facing=%d anim=%d frame=%d frame_count=%d offset=%d,%d dst=%d,%d,%d,%d",
                          i, effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->age_ms, effect->duration_ms, effect->facing_code,
                          effect->frame_ms > 0 ? effect->age_ms / effect->frame_ms : 0,
                          frame, sprite->frame_count,
                          effect->screen_offset_x, effect->screen_offset_y,
                          dst.x, dst.y, dst.w, dst.h);
        SDL_RendererFlip flip = (effect->render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        if ((effect->render_flags & RTS_FRAME_ADDITIVE) != 0)
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
        if ((effect->render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
            SDL_SetTextureColorMod(sprite->texture, 255, 236, 72);
            SDL_SetTextureAlphaMod(sprite->texture, 230);
        }
        SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
        if ((effect->render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
            SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(sprite->texture, 255);
        }
        if ((effect->render_flags & RTS_FRAME_ADDITIVE) != 0)
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
    }
}

void handle_event(App *app, const GameMap *map, Unit *units, int unit_count, const SDL_Event *e) {
    switch (e->type) {
        case SDL_QUIT:
            app->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                refresh_app_viewport(app);
            }
            break;
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_ESCAPE) app->running = false;
            if (e->key.keysym.sym == SDLK_g) app->show_grid = !app->show_grid;
            if (e->key.keysym.sym == SDLK_b) app->show_blocked = !app->show_blocked;
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) {
                for (int i = 0; i < unit_count; ++i) {
                    units[i].selected = units[i].owner == 0 &&
                        (units[i].traits & RTS_TRAIT_SELECTABLE) != 0 &&
                        units[i].hp > 0;
                }
            }
            break;
        case SDL_MOUSEMOTION:
            window_to_render_point(app, e->motion.x, e->motion.y, &app->mouse_x, &app->mouse_y);
            if (app->panning) {
                float dx = 0.0f, dy = 0.0f;
                window_to_render_delta(app, e->motion.xrel, e->motion.yrel, &dx, &dy);
                app->cam_x += dx;
                app->cam_y += dy;
            }
            if (app->dragging_select) {
                int mx = 0, my = 0;
                window_to_render_point(app, e->motion.x, e->motion.y, &mx, &my);
                app->selection_rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, mx, my);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            window_to_render_point(app, e->button.x, e->button.y, &app->mouse_down_x, &app->mouse_down_y);
            if (e->button.button == SDL_BUTTON_LEFT) {
                app->dragging_select = true;
                app->selection_rect = (SDL_Rect){ app->mouse_down_x, app->mouse_down_y, 0, 0 };
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                int rx = 0, ry = 0;
                window_to_render_point(app, e->button.x, e->button.y, &rx, &ry);
                float gx = 0.0f, gy = 0.0f;
                screen_to_grid_point(app, rx, ry, &gx, &gy);
                int target = pick_unit_at(app, units, unit_count, rx, ry, -1);
                if (target >= 0 && units[target].owner != 0 && units[target].hp > 0) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (!units[i].selected || units[i].owner != 0 || units[i].hp <= 0) continue;
                        if ((units[i].traits & RTS_TRAIT_ATTACK) == 0) continue;
                        units[i].attack_target = target;
                        units[i].harvest_target = -1;
                        units[i].harvest_timer_ms = 0;
                    }
                    gx = units[target].gx;
                    gy = units[target].gy;
                } else {
                    if (rts_issue_harvest_order_at(map, units, unit_count, gx, gy)) {
                        break;
                    }
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].selected && units[i].owner == 0) {
                            units[i].attack_target = -1;
                        }
                    }
                }
                rts_issue_move_order_at(map, units, unit_count, gx, gy);
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && app->dragging_select) {
                int bx = 0, by = 0;
                window_to_render_point(app, e->button.x, e->button.y, &bx, &by);
                SDL_Rect rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, bx, by);
                bool box = rect.w > 5 || rect.h > 5;
                bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (!additive) {
                    for (int i = 0; i < unit_count; ++i) units[i].selected = false;
                }
                if (box) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].hp <= 0) continue;
                        if ((units[i].traits & RTS_TRAIT_SELECTABLE) == 0) continue;
                        if (units[i].owner != 0) continue;
                        float sx, sy;
                        grid_to_screen(app, units[i].gx, units[i].gy, &sx, &sy);
                        float radius = unit_pick_radius_px(app, &units[i]);
                        if (point_in_rect((int)sx, (int)sy, rect) ||
                            circle_intersects_rect(sx, sy, radius, rect)) {
                            units[i].selected = true;
                        }
                    }
                } else {
                    int picked = pick_unit_at(app, units, unit_count, bx, by, 0);
                    if (picked >= 0) {
                        units[picked].selected = true;
                    }
                }
                app->dragging_select = false;
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = false;
            }
            break;
        case SDL_MOUSEWHEEL:
            app->cam_y += (float)e->wheel.y * 48.0f * (float)app_scale(app);
            app->cam_x += (float)e->wheel.x * 48.0f * (float)app_scale(app);
            break;
        default:
            break;
    }
}

void update_camera_from_keyboard(App *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt * (float)app_scale(app);
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) app->cam_x += speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) app->cam_x -= speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) app->cam_y += speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) app->cam_y -= speed;
}

void clamp_camera_to_map(App *app, const GameMap *map, int viewport_w, int viewport_h) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    if (viewport_w <= 0) viewport_w = app->win_w;
    if (viewport_h <= 0) viewport_h = app->win_h;

    float map_w = (float)map->width * (float)app_cell_w(app);
    float map_h = (float)map->height * (float)app_cell_h(app);
    if (map_w <= (float)viewport_w) {
        app->cam_x = ((float)viewport_w - map_w) * 0.5f;
    } else {
        float min_x = (float)viewport_w - map_w;
        if (app->cam_x < min_x) app->cam_x = min_x;
        if (app->cam_x > 0.0f) app->cam_x = 0.0f;
    }
    if (map_h <= (float)viewport_h) {
        app->cam_y = ((float)viewport_h - map_h) * 0.5f;
    } else {
        float min_y = (float)viewport_h - map_h;
        if (app->cam_y < min_y) app->cam_y = min_y;
        if (app->cam_y > 0.0f) app->cam_y = 0.0f;
    }
}

void destroy_tileset(Tileset *tileset) {
    if (tileset->texture) SDL_DestroyTexture(tileset->texture);
    free(tileset->tile_lookup);
    free(tileset->animations);
    memset(tileset, 0, sizeof(*tileset));
}

void destroy_map(GameMap *map) {
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_flip_flags[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    memset(map, 0, sizeof(*map));
}

void destroy_sprite(SpriteSheet *sprite) {
    if (sprite->texture) SDL_DestroyTexture(sprite->texture);
    free(sprite->frames);
    free(sprite->frame_bounds);
    memset(sprite, 0, sizeof(*sprite));
}

void destroy_font(RtsBitmapFont *font) {
    if (!font) return;
    destroy_sprite(&font->sprite);
    memset(font, 0, sizeof(*font));
}

void destroy_sprite_cache(SpriteCache *cache) {
    for (int i = 0; i < cache->count; ++i) {
        destroy_sprite(&cache->entries[i].sprite);
    }
    memset(cache, 0, sizeof(*cache));
}
