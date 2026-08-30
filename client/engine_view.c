#define _DEFAULT_SOURCE
#include "engine_internal.h"

static SDL_Rect sprite_visible_bounds(const SpriteSheet *sprite, int frame);
static SDL_Rect sprite_frame_rect(const SpriteSheet *sprite, int frame);

static int app_cell_w(const App *app) {
    return app->cell_w > 0 ? app->cell_w : CELL_W;
}

static int app_cell_h(const App *app) {
    return app->cell_h > 0 ? app->cell_h : CELL_H;
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
    (void)app;
    return tileset->tile_w;
}

static int app_tile_h(const App *app, const Tileset *tileset) {
    (void)app;
    return tileset->tile_h;
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

void map_grid_to_screen(const App *app, const GameMap *map, float gx, float gy,
                        float *sx, float *sy) {
    float screen_y = map_screen_y_for_point(map, gy);
    grid_to_screen(app, gx, screen_y, sx, sy);
}

static void screen_to_grid_point(const App *app, int sx, int sy, float *gx, float *gy) {
    if (gx) *gx = ((float)sx - app->cam_x) / (float)app_cell_w(app);
    if (gy) *gy = ((float)sy - app->cam_y) / (float)app_cell_h(app);
}

Cell screen_to_grid(const App *app, int sx, int sy) {
    return (Cell){ (int)floorf(((float)sx - app->cam_x) / (float)app_cell_w(app)),
                   (int)floorf(((float)sy - app->cam_y) / (float)app_cell_h(app)) };
}

Cell screen_to_map_grid(const App *app, const GameMap *map, int sx, int sy) {
    float gx = 0.0f, gy = 0.0f;
    screen_to_grid_point(app, sx, sy, &gx, &gy);
    gy = map_world_y_from_screen_point(map, gy);
    return (Cell){ (int)floorf(gx), (int)floorf(gy) };
}

static void screen_to_map_grid_point(const App *app, const GameMap *map, int sx, int sy,
                                     float *gx, float *gy) {
    screen_to_grid_point(app, sx, sy, gx, gy);
    if (gy) *gy = map_world_y_from_screen_point(map, *gy);
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

static void render_map_grid_cell(App *app, const GameMap *map, int gx, int gy, SDL_Color color) {
    float sx, sy;
    map_grid_to_screen(app, map, (float)gx, (float)gy, &sx, &sy);
    SDL_Rect r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
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
            map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
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
    int draw_y_offset = tileset->draw_y_offset;
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            float sx, sy;
            map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
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
                map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
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
                map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
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
                map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                render_map_grid_cell(app, map, x, y, (SDL_Color){ 50, 78, 72, 80 });
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
        debug_effects_log("sequence miss sprite_frames=%d sequence=%s facing=%d anim=%d",
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
        debug_effects_log("sequence bad start sequence=%s facing=%d/%d code=%d start=%d frame_count=%d",
                          sequence_name, facing, seq->facings, facing_code, start,
                          sprite ? sprite->frame_count : 0);
        return -1;
    }
    int frame = start + anim * frame_stride;
    if (frame < 0) return start;
    if (frame >= sprite->frame_count) return sprite->frame_count - 1;
    return frame;
}


static int decoration_sprite_frame(App *app, const MapDecoration *dec, const SpriteSheet *sprite,
                                   int frame_index, const char *sequence_name) {
    int frame = -1;
    if (sequence_name && sequence_name[0] != '\0') {
        frame = sprite_sequence_frame(sprite, sequence_name, dec->facing_code, frame_index);
    }
    if (frame >= 0) return frame;
    if (frame_index >= 0 && frame_index < sprite->frame_count) return frame_index;
    if (frame_index < 0) {
        return (int)((app->ticks_ms / 250u) % (uint32_t)sprite->frame_count);
    }
    return 0;
}

static uint8_t fin_intensity_color_mod(int intensity) {
    if (intensity <= 0) intensity = 16;
    return (uint8_t)clamp255((intensity * 255 + 8) / 16);
}

static SDL_Texture *sprite_texture_for_remap(const SpriteSheet *sprite, int render_remap) {
    if (!sprite) return NULL;
    if (render_remap > 0 && render_remap < 8 && sprite->remap_textures[render_remap])
        return sprite->remap_textures[render_remap];
    return sprite->texture;
}

static SDL_Texture *begin_sprite_command(const SpriteSheet *sprite, uint32_t render_flags,
                                         int render_remap, int render_intensity) {
    SDL_Texture *texture = sprite_texture_for_remap(sprite, render_remap);
    if (!texture) return NULL;
    if ((render_flags & RTS_FRAME_ADDITIVE) != 0)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);

    uint8_t intensity = fin_intensity_color_mod(render_intensity);
    uint8_t r = intensity;
    uint8_t g = intensity;
    uint8_t b = intensity;
    uint8_t a = 255;
    if ((render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
        r = intensity;
        g = (uint8_t)((intensity * 236 + 127) / 255);
        b = (uint8_t)((intensity * 72 + 127) / 255);
        a = 230;
    }
    SDL_SetTextureColorMod(texture, r, g, b);
    SDL_SetTextureAlphaMod(texture, a);
    return texture;
}

static void end_sprite_command(SDL_Texture *texture, uint32_t render_flags) {
    if (!texture) return;
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    if ((render_flags & RTS_FRAME_ADDITIVE) != 0)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
}

static SDL_Point sprite_frame_raw_displacement(const SpriteSheet *sprite, int frame);
static SDL_Point sprite_ground_point(const SpriteSheet *sprite, int frame);

static void render_decoration_sprite(App *app, const GameMap *map,
                                     const MapDecoration *dec, const SpriteSheet *sprite,
                                     int frame_index, uint32_t render_flags,
                                     const char *sequence_name, int anchor_frame_index,
                                     const char *anchor_sequence_name) {
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    map_grid_to_screen(app, map, (float)dec->gx, (float)dec->gy, &sx, &sy);
    int footprint_w = dec->footprint_w > 0 ? dec->footprint_w : 1;
    int footprint_h = dec->footprint_h > 0 ? dec->footprint_h : 1;

    int frame = decoration_sprite_frame(app, dec, sprite, frame_index, sequence_name);
    int anchor_frame = decoration_sprite_frame(app, dec, sprite, anchor_frame_index,
                                               anchor_sequence_name);
    SDL_Rect frame_rect = sprite_frame_rect(sprite, frame);
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;

    SDL_Rect dst;
    if (dec->has_sprite_pivot) {
        /* The plugin-authored point is in the full frame canvas, not in the
           visible-pixel bounds.  All layers of a composite therefore attach
           to exactly the same world point. */
        if (dec->center_anchor) {
            map_grid_to_screen(app, map,
                               (float)dec->gx + (float)footprint_w * 0.5f,
                               (float)dec->gy + (float)footprint_h * 0.5f,
                               &sx, &sy);
        }
        dst = (SDL_Rect){
            (int)lroundf(sx) - dec->sprite_pivot_x,
            (int)lroundf(sy) - dec->sprite_pivot_y,
            sprite_w,
            sprite_h,
        };
    } else if (dec->center_anchor) {
        map_grid_to_screen(app, map,
                           (float)dec->gx + (float)footprint_w * 0.5f,
                           (float)dec->gy + (float)footprint_h * 0.5f,
                           &sx, &sy);
        SDL_Point ground;
        if (sprite->frame_ground_points) {
            ground = sprite_ground_point(sprite, anchor_frame);
        } else {
            /* Preserve the legacy decoration anchor for formats without an
               authored ground point (notably Dark Colony FIN sprites). */
            SDL_Rect anchor_rect = sprite_frame_rect(sprite, anchor_frame);
            SDL_Rect bounds = sprite_visible_bounds(sprite, anchor_frame);
            ground = (SDL_Point){ anchor_rect.w / 2, bounds.y + bounds.h };
        }
        dst = (SDL_Rect){
            (int)(sx - ground.x),
            (int)(sy - ground.y),
            sprite_w,
            sprite_h,
        };
        if (frame != anchor_frame) {
            SDL_Point anchor_dis = sprite_frame_raw_displacement(sprite, anchor_frame);
            SDL_Point frame_dis = sprite_frame_raw_displacement(sprite, frame);
            dst.x += frame_dis.x - anchor_dis.x;
            dst.y += frame_dis.y - anchor_dis.y;
        }
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
    if ((render_flags & RTS_FRAME_BLINK) != 0 && ((app->ticks_ms / 250u) % 2u) == 0u) {
        return;
    }
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_Texture *texture = begin_sprite_command(sprite, render_flags, 0, 16);
    SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    end_sprite_command(texture, render_flags);
}

static void render_decoration(App *app, const GameMap *map,
                              const MapDecoration *dec, const SpriteCache *cache) {
    render_decoration_sprite(app, map, dec, sprite_cache_lookup(cache, dec->shadow_name),
                             dec->frame_index, dec->render_flags, dec->sequence_name,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, sprite_cache_lookup(cache, dec->sprite_name),
                             dec->frame_index, dec->render_flags, dec->sequence_name,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, sprite_cache_lookup(cache, dec->sprite2_name),
                             dec->frame2_index, dec->render2_flags, NULL,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, sprite_cache_lookup(cache, dec->sprite3_name),
                             dec->frame3_index, dec->render3_flags, NULL,
                             dec->frame_index, dec->sequence_name);
}

void render_decorations(App *app, const GameMap *map, const SpriteCache *cache) {
    for (int i = 0; i < map->decoration_count; ++i) {
        render_decoration(app, map, &map->decorations[i], cache);
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
    float radius = unit_radius_cells(unit) * cell;
    float min_radius = 12.0f;
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

static SDL_Rect sprite_frame_rect(const SpriteSheet *sprite, int frame) {
    if (sprite && sprite->frames && frame >= 0 && frame < sprite->frame_count &&
        sprite->frames[frame].w > 0 && sprite->frames[frame].h > 0) {
        return sprite->frames[frame];
    }
    return (SDL_Rect){ 0, 0, sprite ? sprite->frame_w : 1, sprite ? sprite->frame_h : 1 };
}

static SDL_Point sprite_frame_raw_displacement(const SpriteSheet *sprite, int frame) {
    if (sprite && sprite->frame_displacements && frame >= 0 && frame < sprite->frame_count) {
        return sprite->frame_displacements[frame];
    }
    return (SDL_Point){ 0, 0 };
}

static SDL_Point sprite_frame_displacement(const SpriteSheet *sprite, int frame,
                                           uint32_t render_flags) {
    SDL_Point p = { 0, 0 };
    if (sprite && sprite->frame_displacements && frame >= 0 && frame < sprite->frame_count) {
        p = sprite->frame_displacements[frame];
    }
    p.y = 0;
    if ((render_flags & RTS_FRAME_FLIP_X) != 0) p.x = 0;
    return p;
}

static SDL_Point sprite_ground_point(const SpriteSheet *sprite, int frame) {
    if (sprite && sprite->frame_ground_points && frame >= 0 && frame < sprite->frame_count) {
        return sprite->frame_ground_points[frame];
    }
    SDL_Rect bounds = sprite_visible_bounds(sprite, frame);
    return (SDL_Point){ bounds.x + bounds.w / 2, bounds.y + bounds.h };
}

static const SpriteSheet *unit_sprite_sheet_for_view(const Unit *unit,
                                                     const SpriteSheet *fallback_sprite,
                                                     const SpriteCache *cache,
                                                     const GameInfo *game_info) {
    if (!unit) return NULL;
    const char *sprite_name = unit->sprite_name;
    if (game_info && unit->sprite_id >= 0 && unit->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[unit->sprite_id]) {
        sprite_name = game_info->sprnames[unit->sprite_id];
    }
    const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
    return sprite ? sprite : fallback_sprite;
}

static int unit_frame_for_view(const SpriteSheet *sprite, const Unit *unit,
                               const GameInfo *game_info, uint32_t ticks) {
    /* FIN/state-driven games author the resolved frame on the unit.  Dark Reign
       has a GameInfo for shared simulation metadata, but its RSPR facings are
       selected by the renderer from the unit heading. */
    bool has_state_frames = game_info && game_info->states && game_info->state_count > 0;
    int frame = has_state_frames ? unit->frame : sprite_frame_for_unit(sprite, unit, ticks);
    if (!sprite || sprite->frame_count <= 0) return 0;
    if (frame >= sprite->frame_count) frame = 0;
    if (frame < 0) frame = 0;
    return frame;
}

static int direction_slot_for_view(int facings, const int *direction_codes, int facing_code) {
    if (!direction_codes || facings <= 0) return -1;
    int wrap = 16;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        if (direction_codes[i] > 7 || facing_code > 7) {
            wrap = 16;
            break;
        }
        wrap = 8;
    }
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta > wrap / 2) delta = wrap - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static void unit_state_body_offset_for_view(const GameInfo *game_info, const Unit *unit,
                                            int *offset_x, int *offset_y) {
    int ox = 0;
    int oy = 0;
    if (game_info && unit && game_info->states &&
        unit->state_id >= 0 && unit->state_id < game_info->state_count) {
        const State *state = &game_info->states[unit->state_id];
        if (state->facings > 0) {
            int slot = direction_slot_for_view(state->facings, state->direction_codes,
                                               unit->facing_code);
            if (slot < 0) slot = 0;
            if (slot < RTS_MAX_STATE_FACINGS) {
                ox = state->offset_x[slot];
                oy = state->offset_y[slot];
            }
        }
    }
    if (offset_x) *offset_x = ox;
    if (offset_y) *offset_y = oy;
}

static bool unit_screen_rect_for_view(const App *app, const GameMap *map, const Unit *unit,
                                      const SpriteSheet *fallback_sprite,
                                      const SpriteCache *cache,
                                      const GameInfo *game_info, uint32_t ticks,
                                      SDL_Rect *dst_out, SDL_Rect *visible_out,
                                      float *sx_out, float *sy_out,
                                      int *frame_out, const SpriteSheet **sprite_out) {
    if (!app || !unit) return false;
    float sx = 0.0f, sy = 0.0f;
    map_grid_to_screen(app, map, unit->gx, unit->gy, &sx, &sy);
    const SpriteSheet *sprite = unit_sprite_sheet_for_view(unit, fallback_sprite, cache, game_info);
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
        float radius = unit_pick_radius_px(app, unit);
        SDL_Rect fallback = {
            (int)floorf(sx - radius),
            (int)floorf(sy - radius),
            (int)ceilf(radius * 2.0f),
            (int)ceilf(radius * 2.0f),
        };
        if (dst_out) *dst_out = fallback;
        if (visible_out) *visible_out = fallback;
        if (sx_out) *sx_out = sx;
        if (sy_out) *sy_out = sy;
        if (frame_out) *frame_out = 0;
        if (sprite_out) *sprite_out = sprite;
        return false;
    }

    int frame = unit_frame_for_view(sprite, unit, game_info, ticks);
    SDL_Rect frame_rect = sprite_frame_rect(sprite, frame);
    SDL_Rect bounds = sprite_visible_bounds(sprite, frame);
    uint32_t render_flags = game_info ? unit->render_flags : 0;
    if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
        bounds.x = frame_rect.w - bounds.x - bounds.w;
    }
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;
    int body_offset_x = 0;
    int body_offset_y = 0;
    unit_state_body_offset_for_view(game_info, unit, &body_offset_x, &body_offset_y);
    SDL_Rect dst;
    if (game_info && game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
        SDL_Point dis = sprite_frame_displacement(sprite, frame, render_flags);
        dst = (SDL_Rect){
            (int)lroundf(sx) + body_offset_x + dis.x,
            (int)lroundf(sy) + body_offset_y + dis.y - sprite_h,
            sprite_w,
            sprite_h,
        };
    } else {
        SDL_Point ground = sprite_ground_point(sprite, frame);
        if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
            ground.x = frame_rect.w - ground.x;
        }
        dst = (SDL_Rect){
            (int)lroundf(sx - (float)ground.x) + body_offset_x,
            (int)lroundf(sy - (float)ground.y) + body_offset_y,
            sprite_w,
            sprite_h,
        };
    }
    dst.x += unit->render_offset_x;
    dst.y += unit->render_offset_y;
    SDL_Rect visible = {
        dst.x + bounds.x,
        dst.y + bounds.y,
        bounds.w,
        bounds.h,
    };
    if (dst_out) *dst_out = dst;
    if (visible_out) *visible_out = visible;
    if (sx_out) *sx_out = sx;
    if (sy_out) *sy_out = sy;
    if (frame_out) *frame_out = frame;
    if (sprite_out) *sprite_out = sprite;
    return true;
}

static bool rects_intersect(SDL_Rect a, SDL_Rect b) {
    return a.x <= b.x + b.w && a.x + a.w >= b.x &&
           a.y <= b.y + b.h && a.y + a.h >= b.y;
}

static int pick_unit_at(const App *app, const GameMap *map, const Unit *units, int unit_count,
                        const SpriteSheet *fallback_sprite, const SpriteCache *cache,
                        const GameInfo *game_info, int x, int y, int owner_filter) {
    int best = -1;
    float best_score = 1000000000.0f;
    for (int i = unit_count - 1; i >= 0; --i) {
        const Unit *unit = &units[i];
        if (unit->hp <= 0 || (unit->traits & RTS_TRAIT_SELECTABLE) == 0) continue;
        if (owner_filter >= 0 && unit->owner != owner_filter) continue;
        SDL_Rect visible;
        float sx = 0.0f, sy = 0.0f;
        unit_screen_rect_for_view(app, map, unit, fallback_sprite, cache, game_info, app->ticks_ms,
                                  NULL, &visible, &sx, &sy, NULL, NULL);
        if (!point_in_rect(x, y, visible)) continue;
        float dx = (float)x - sx;
        float dy = (float)y - sy;
        float score = dx * dx + dy * dy;
        if (score >= best_score) continue;
        best_score = score;
        best = i;
    }
    return best;
}

static int selection_health_bucket(const Unit *u) {
    if (!u || u->max_hp <= 0) return 0;
    if (u->hp * 3 <= u->max_hp) return 2;
    if (u->hp * 3 <= u->max_hp * 2) return 1;
    return 0;
}

static SDL_Color selection_health_tint(int bucket) {
    switch (bucket) {
    case 2: return (SDL_Color){ 255, 76, 54, 255 };
    case 1: return (SDL_Color){ 255, 218, 62, 255 };
    default: return (SDL_Color){ 83, 245, 92, 255 };
    }
}

static void draw_ellipse_outline(SDL_Renderer *renderer, int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) return;
    int steps = (rx + ry) * 2;
    if (steps < 16) steps = 16;
    for (int i = 0; i < steps; ++i) {
        float a = (float)i / (float)steps * 6.28318530f;
        int x = (int)(rx * cosf(a) + 0.5f);
        int y = (int)(ry * sinf(a) + 0.5f);
        SDL_RenderDrawPoint(renderer, cx + x, cy + y);
    }
}

static void draw_selection_circle(App *app, const Unit *u, int cx, int cy, int radius) {
    if (!app || !app->renderer || radius < 2) return;
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
    int rx = radius;
    int ry = radius / 2;
    if (ry < 2) ry = 2;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));
    SDL_SetRenderDrawColor(app->renderer, 8, 10, 8, 255);
    draw_ellipse_outline(app->renderer, cx, cy + 1, rx + 1, ry);
    draw_ellipse_outline(app->renderer, cx, cy + 1, rx,     ry);
    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, 255);
    draw_ellipse_outline(app->renderer, cx, cy, rx + 1, ry);
    draw_ellipse_outline(app->renderer, cx, cy, rx,     ry);
}

static void draw_selection_brackets(App *app, const Unit *u, const SDL_Rect *visible) {
    if (!app || !app->renderer || !u || !visible || visible->w <= 0 || visible->h <= 0) return;
    SDL_Rect box = {
        visible->x - 3,
        visible->y - 3,
        visible->w + 6,
        visible->h + 6,
    };
    int corner = box.w < box.h ? box.w / 4 : box.h / 4;
    if (corner < 4) corner = 4;
    if (corner > 9) corner = 9;
    int right = box.x + box.w;
    int bottom = box.y + box.h;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 236, 236, 220, 230);
    SDL_RenderDrawLine(app->renderer, box.x, box.y, box.x + corner, box.y);
    SDL_RenderDrawLine(app->renderer, box.x, box.y, box.x, box.y + corner);
    SDL_RenderDrawLine(app->renderer, right - corner, box.y, right, box.y);
    SDL_RenderDrawLine(app->renderer, right, box.y, right, box.y + corner);
    SDL_RenderDrawLine(app->renderer, box.x, bottom - corner, box.x, bottom);
    SDL_RenderDrawLine(app->renderer, box.x, bottom, box.x + corner, bottom);
    SDL_RenderDrawLine(app->renderer, right, bottom - corner, right, bottom);
    SDL_RenderDrawLine(app->renderer, right - corner, bottom, right, bottom);

    int bar_w = box.w - 8;
    if (bar_w < 16) bar_w = 16;
    int bar_x = box.x + (box.w - bar_w) / 2;
    int bar_y = box.y - 5;
    int fill_w = u->max_hp > 0 ? (bar_w * u->hp) / u->max_hp : bar_w;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));
    SDL_SetRenderDrawColor(app->renderer, 20, 20, 18, 235);
    SDL_RenderFillRect(app->renderer, &(SDL_Rect){ bar_x - 1, bar_y - 1, bar_w + 2, 4 });
    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, 255);
    SDL_RenderFillRect(app->renderer, &(SDL_Rect){ bar_x, bar_y, fill_w, 2 });
}

static void draw_selection_triangle(App *app, const Unit *u, const SDL_Rect *visible) {
    if (!app || !app->renderer || !visible || visible->w <= 0 || visible->h <= 0) return;
    int cx = visible->x + visible->w / 2;
    int top_y = visible->y - 11;
    int tip_y = top_y + 7;
    int half_w = 8;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));

    SDL_SetRenderDrawColor(app->renderer, 8, 10, 8, 235);
    SDL_RenderDrawLine(app->renderer, cx - half_w - 1, top_y - 1, cx + half_w + 1, top_y - 1);
    SDL_RenderDrawLine(app->renderer, cx - half_w - 1, top_y - 1, cx, tip_y + 1);
    SDL_RenderDrawLine(app->renderer, cx + half_w + 1, top_y - 1, cx, tip_y + 1);

    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, tint.a);
    for (int y = top_y; y <= tip_y; ++y) {
        float t = (float)(y - top_y) / (float)(tip_y - top_y);
        int span = (int)lroundf((float)half_w * (1.0f - t));
        SDL_RenderDrawLine(app->renderer, cx - span, y, cx + span, y);
    }
}

static bool draw_selection_marker_sprite(App *app, const Unit *u, const SpriteCache *cache,
                                         const GameInfo *game_info, const SDL_Rect *visible) {
    if (!app || !app->renderer || !u || !cache || !game_info || !visible ||
        !game_info->sprnames) {
        return false;
    }
    const SelectionMarkerInfo *info = &game_info->selection_marker;
    if (info->sprite < 0 || info->sprite >= game_info->sprite_count) return false;
    const char *sprite_name = game_info->sprnames[info->sprite];
    const SpriteSheet *marker = sprite_cache_lookup(cache, sprite_name);
    if (!marker || !marker->texture || marker->frame_count <= 0) return false;

    int bucket = selection_health_bucket(u);
    int frame = bucket == 2 ? info->critical_frame :
                bucket == 1 ? info->wounded_frame : info->healthy_frame;
    if (frame < 0 || frame >= marker->frame_count) return false;
    SDL_Rect frame_rect = sprite_frame_rect(marker, frame);
    if (frame_rect.w <= 0 || frame_rect.h <= 0) return false;

    int cx = visible->x + visible->w / 2;
    SDL_Rect dst = {
        cx - frame_rect.w / 2,
        visible->y - frame_rect.h + info->top_offset_y,
        frame_rect.w,
        frame_rect.h,
    };
    SDL_Texture *texture = begin_sprite_command(marker, 0, 0, 16);
    if (!texture) return false;
    SDL_RenderCopy(app->renderer, texture, &marker->frames[frame], &dst);
    end_sprite_command(texture, 0);
    return true;
}

static void render_unit_state_overlay(App *app, const Unit *u, const SpriteSheet *body_sprite,
                                      int body_frame, const SpriteCache *cache,
                                      const GameInfo *game_info, const SDL_Rect *body_dst,
                                      float origin_sx, float origin_sy) {
    (void)body_sprite;
    (void)body_frame;
    if (!app || !u || !cache || !game_info || !body_dst ||
        u->state_id < 0 || u->state_id >= game_info->state_count ||
        !game_info->states || !game_info->sprnames) {
        return;
    }
    const State *state = &game_info->states[u->state_id];
    if (state->overlay_facings <= 0 || state->overlay_sprite < 0 ||
        state->overlay_sprite >= game_info->sprite_count) {
        return;
    }
    int slot = direction_slot_for_view(state->overlay_facings,
                                       state->overlay_direction_codes,
                                       u->facing_code);
    if (slot < 0) slot = 0;
    if (slot >= RTS_MAX_STATE_FACINGS) return;
    int frame = state->overlay_facing_frames[slot];
    if (frame < 0) return;

    const char *sprite_name = game_info->sprnames[state->overlay_sprite];
    const SpriteSheet *overlay = sprite_cache_lookup(cache, sprite_name);
    if (!overlay || !overlay->texture || overlay->frame_count <= 0) return;
    if (frame >= overlay->frame_count) frame = 0;
    SDL_Rect frame_rect = sprite_frame_rect(overlay, frame);

    uint32_t flags = state->overlay_facing_flags[slot];
    SDL_Rect dst;
    if (game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
        SDL_Point dis = sprite_frame_displacement(overlay, frame, flags);
        dst = (SDL_Rect){
            (int)lroundf(origin_sx) + state->overlay_offset_x[slot] + dis.x,
            (int)lroundf(origin_sy) + state->overlay_offset_y[slot] + dis.y - frame_rect.h,
            frame_rect.w,
            frame_rect.h,
        };
    } else {
        dst = (SDL_Rect){
            body_dst->x + state->overlay_offset_x[slot],
            body_dst->y + state->overlay_offset_y[slot],
            frame_rect.w,
            frame_rect.h,
        };
    }
    SDL_RendererFlip flip = (flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    int remap = state->overlay_remap[slot];
    int intensity = state->overlay_intensity[slot];
    SDL_Texture *texture = begin_sprite_command(overlay, flags, remap, intensity);
    SDL_RenderCopyEx(app->renderer, texture, &overlay->frames[frame],
                     &dst, 0.0, NULL, flip);
    end_sprite_command(texture, flags);
}

static void render_unit_sprite(App *app, const GameMap *map,
                               const Unit *u, const SpriteSheet *fallback_sprite,
                               const SpriteCache *cache, const GameInfo *game_info,
                               uint32_t ticks) {
    if (!u || (u->traits & RTS_TRAIT_RENDERABLE) == 0) return;
    const SpriteSheet *sprite = unit_sprite_sheet_for_view(u, fallback_sprite, cache, game_info);
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx = 0.0f, sy = 0.0f;
    int frame = 0;
    SDL_Rect dst;
    SDL_Rect visible;
    unit_screen_rect_for_view(app, map, u, fallback_sprite, cache, game_info, ticks,
                              &dst, &visible, &sx, &sy, &frame, &sprite);
    uint32_t render_flags = game_info ? u->render_flags : 0;
    const SpriteSheet *shadow = sprite_cache_lookup(cache, u->shadow_name);
    /* Some Dark Reign unit definitions repeat the body RSPR in
       SetShadowImage.  The original treats its shadow data specially; our
       cache resolves that name to the already-loaded colour body sheet, so
       drawing it here would create a second vehicle that mirrors every move. */
    if (shadow && shadow != sprite && shadow->texture && shadow->frame_count > 0) {
        int shadow_frame = frame < shadow->frame_count ? frame : 0;
        SDL_Rect shadow_rect = sprite_frame_rect(shadow, shadow_frame);
        SDL_Rect shadow_dst = { dst.x, dst.y, shadow_rect.w, shadow_rect.h };
        SDL_RenderCopy(app->renderer, shadow->texture, &shadow->frames[shadow_frame], &shadow_dst);
    }
    float content_y = (float)visible.y;
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_Texture *texture = begin_sprite_command(sprite, render_flags, u->render_remap,
                                                u->render_intensity);
    SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    end_sprite_command(texture, render_flags);
    render_unit_state_overlay(app, u, sprite, frame, cache, game_info, &dst, sx, sy);
    if (u->selected && (u->traits & RTS_TRAIT_SELECTABLE) != 0) {
        SelectionStyle sel_style = game_info ? game_info->selection_marker.style
                                             : SELECTION_STYLE_SPRITE;
        if (sel_style == SELECTION_STYLE_CIRCLE) {
            int radius = (int)(unit_pick_radius_px(app, u) * 0.85f);
            draw_selection_circle(app, u, (int)sx, (int)sy, radius);
        }
        else if (sel_style == SELECTION_STYLE_BRACKETS)
            draw_selection_brackets(app, u, &visible);
        else if (!draw_selection_marker_sprite(app, u, cache, game_info, &visible))
            draw_selection_triangle(app, u, &visible);
    }
    if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp &&
        (!game_info || game_info->selection_marker.style != SELECTION_STYLE_BRACKETS ||
         !u->selected)) {
        int bar_w = dst.w / 2;
        int bar_h = 2;
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
                  const SpriteCache *cache, const GameInfo *game_info, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        render_unit_sprite(app, NULL, &units[i], fallback_sprite, cache, game_info, ticks);
    }
}

static int compare_draw_commands(const void *a, const void *b) {
    const DrawCommand *ia = a;
    const DrawCommand *ib = b;
    if (ia->sort_y < ib->sort_y) return -1;
    if (ia->sort_y > ib->sort_y) return 1;
    if (ia->layer != ib->layer) return (int)ia->layer - (int)ib->layer;
    if (ia->kind != ib->kind) return (int)ia->kind - (int)ib->kind;
    return ia->stable_index - ib->stable_index;
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
    int draw_y_offset = tileset->draw_y_offset;
    float sx, sy;
    map_grid_to_screen(app, map, (float)x, (float)y, &sx, &sy);
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
                          const SpriteCache *cache, const GameInfo *game_info, uint32_t ticks) {
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
    DrawCommand *commands = malloc((size_t)total * sizeof(*commands));
    if (!commands) {
        render_decorations(app, map, cache);
        for (int i = 0; i < unit_count; ++i) {
            render_unit_sprite(app, map, &units[i], fallback_sprite, cache, game_info, ticks);
        }
        return;
    }

    int count = 0;
    if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][map_index(map, x, y)] <= 0) continue;
                    commands[count++] = (DrawCommand){
                        .kind = DRAW_COMMAND_TILE_OVERLAY,
                        .layer = RENDER_LAYER_TERRAIN_OVERLAY,
                        .sort_y = map_screen_y_for_cell(map, y) + 1.0f + (float)layer * 0.001f,
                        .stable_index = map_index(map, x, y),
                        .ref.tile_overlay = { .x = x, .y = y, .layer = layer },
                    };
                }
            }
        }
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        float sort_y = dec->has_sprite_pivot ?
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1) :
            dec->center_anchor ?
            (float)dec->gy + 0.5f :
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1);
        sort_y = map_screen_y_for_point(map, sort_y);
        commands[count++] = (DrawCommand){
            .kind = DRAW_COMMAND_DECORATION,
            .layer = RENDER_LAYER_DECORATION,
            .sort_y = sort_y,
            .stable_index = i,
            .ref.decoration = dec,
        };
    }
    for (int i = 0; i < unit_count; ++i) {
        float sort_y = units[i].render_sort_y > 0.0f ? units[i].render_sort_y : units[i].gy;
        sort_y = map_screen_y_for_point(map, sort_y);
        commands[count++] = (DrawCommand){
            .kind = DRAW_COMMAND_UNIT,
            .layer = RENDER_LAYER_UNIT,
            .sort_y = sort_y,
            .stable_index = i,
            .ref.unit = &units[i],
        };
    }

    qsort(commands, (size_t)count, sizeof(*commands), compare_draw_commands);
    for (int i = 0; i < count; ++i) {
        DrawCommand *command = &commands[i];
        if (command->kind == DRAW_COMMAND_TILE_OVERLAY) {
            render_overlay_tile_item(app, map, tileset, command->ref.tile_overlay.x,
                                     command->ref.tile_overlay.y, command->ref.tile_overlay.layer);
        } else if (command->kind == DRAW_COMMAND_DECORATION) {
            render_decoration(app, map, command->ref.decoration, cache);
        } else {
            render_unit_sprite(app, map, command->ref.unit, fallback_sprite, cache, game_info, ticks);
        }
    }
    free(commands);
}

static int sprite_frame_for_effect(const SpriteSheet *sprite, const VisualEffect *effect) {
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

void render_visual_effects(App *app, const GameMap *map,
                           const VisualEffect *effects, int max_effects,
                           const SpriteCache *cache, const GameInfo *game_info) {
    if (!effects || max_effects <= 0) return;
    for (int i = 0; i < max_effects; ++i) {
        const VisualEffect *effect = &effects[i];
        if (!effect->active) continue;
        const char *sprite_name = effect->sprite_name;
        if (effect->use_state && game_info && effect->sprite_id >= 0 &&
            effect->sprite_id < game_info->sprite_count &&
            game_info->sprnames && game_info->sprnames[effect->sprite_id]) {
            sprite_name = game_info->sprnames[effect->sprite_id];
        }
        const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
        if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s reason=missing-cache",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)");
            continue;
        }

        float sx, sy;
        map_grid_to_screen(app, map, effect->gx, effect->gy, &sx, &sy);
        int frame = effect->use_state ? effect->frame : sprite_frame_for_effect(sprite, effect);
        if (frame < 0 || frame >= sprite->frame_count) frame = 0;
        SDL_Rect frame_rect = sprite_frame_rect(sprite, frame);
        int sprite_w = frame_rect.w;
        int sprite_h = frame_rect.h;
        SDL_Rect dst;
        if (effect->use_state && game_info &&
            game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
            SDL_Point dis = sprite_frame_displacement(sprite, frame, effect->render_flags);
            dst = (SDL_Rect){
                (int)lroundf(sx) + effect->screen_offset_x + dis.x,
                (int)lroundf(sy) + effect->screen_offset_y + dis.y - sprite_h,
                sprite_w,
                sprite_h,
            };
        } else {
            dst = (SDL_Rect){
                (int)(sx - sprite_w / 2),
                (int)(sy - sprite_h / 2),
                sprite_w,
                sprite_h,
            };
            if (effect->screen_offset_x != 0 || effect->screen_offset_y != 0) {
                dst.x += effect->screen_offset_x;
                dst.y += effect->screen_offset_y;
            }
        }
        if (dst.x > app->win_w || dst.y > app->win_h ||
            dst.x + dst.w < 0 || dst.y + dst.h < 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s frame_count=%d pos=%.2f,%.2f dst=%d,%d,%d,%d reason=offscreen",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)",
                              sprite->frame_count, effect->gx, effect->gy,
                              dst.x, dst.y, dst.w, dst.h);
            continue;
        }
        debug_effects_log("render slot=%d sprite=%s sequence=%s age=%d/%d facing=%d anim=%d frame=%d frame_count=%d offset=%d,%d dst=%d,%d,%d,%d",
                          i, effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->age_ms, effect->duration_ms, effect->facing_code,
                          effect->frame_ms > 0 ? effect->age_ms / effect->frame_ms : 0,
                          frame, sprite->frame_count,
                          effect->screen_offset_x, effect->screen_offset_y,
                          dst.x, dst.y, dst.w, dst.h);
        SDL_RendererFlip flip = (effect->render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_Texture *texture = begin_sprite_command(sprite, effect->render_flags,
                                                    effect->render_remap,
                                                    effect->render_intensity);
        SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
        end_sprite_command(texture, effect->render_flags);
    }
}

void handle_event(App *app, const GameMap *map, Unit *units, int unit_count,
                  const SpriteSheet *fallback_sprite, const SpriteCache *cache,
                  const GameInfo *game_info, const SDL_Event *e) {
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
                screen_to_map_grid_point(app, map, rx, ry, &gx, &gy);
                int target = pick_unit_at(app, map, units, unit_count, fallback_sprite, cache,
                                          game_info, rx, ry, -1);
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
                    if (issue_harvest_order_at(map, units, unit_count, gx, gy)) {
                        break;
                    }
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].selected && units[i].owner == 0) {
                            units[i].attack_target = -1;
                        }
                    }
                }
                issue_move_order_at(map, units, unit_count, gx, gy);
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
                        SDL_Rect visible;
                        float sx = 0.0f, sy = 0.0f;
                        unit_screen_rect_for_view(app, map, &units[i], fallback_sprite, cache,
                                                  game_info, app->ticks_ms, NULL, &visible,
                                                  &sx, &sy, NULL, NULL);
                        float radius = unit_pick_radius_px(app, &units[i]);
                        if (rects_intersect(visible, rect) ||
                            circle_intersects_rect(sx, sy, radius, rect)) {
                            units[i].selected = true;
                        }
                    }
                } else {
                    int picked = pick_unit_at(app, map, units, unit_count, fallback_sprite, cache,
                                              game_info, bx, by, 0);
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
            app->cam_y += (float)e->wheel.y * 48.0f;
            app->cam_x += (float)e->wheel.x * 48.0f;
            break;
        default:
            break;
    }
}

void update_camera_from_keyboard(App *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt;
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
    free(map->extras);
    if (map->destroy_native_data) map->destroy_native_data(map->native_data);
    memset(map, 0, sizeof(*map));
}

void destroy_sprite(SpriteSheet *sprite) {
    if (!sprite) return;
    if (sprite->texture) SDL_DestroyTexture(sprite->texture);
    for (int i = 0; i < 8; ++i)
        if (sprite->remap_textures[i]) SDL_DestroyTexture(sprite->remap_textures[i]);
    free(sprite->frames);
    free(sprite->frame_bounds);
    free(sprite->frame_ground_points);
    free(sprite->frame_displacements);
    if (sprite->destroy_native_data) sprite->destroy_native_data(sprite->native_data);
    memset(sprite, 0, sizeof(*sprite));
}

void destroy_font(BitmapFont *font) {
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
