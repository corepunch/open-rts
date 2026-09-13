#include "d_net.h"
#define _DEFAULT_SOURCE
#include "p_local.h"
#include "info.h"

static irect_t sprite_visible_bounds(const spritesheet_t *sprite, int frame);
static irect_t sprite_frame_rect(const spritesheet_t *sprite, int frame);

static int app_cell_w(const app_t *app) {
    return app->cell.w > 0 ? app->cell.w : CELL_W;
}

static int app_cell_h(const app_t *app) {
    return app->cell.h > 0 ? app->cell.h : CELL_H;
}

static float viewport_scale_x(const app_t *app) {
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

static float viewport_scale_y(const app_t *app) {
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

static int app_tile_w(const app_t *app, const tileset_t *tileset) {
    (void)app;
    return tileset->tile_w;
}

static int app_tile_h(const app_t *app, const tileset_t *tileset) {
    (void)app;
    return tileset->tile_h;
}

static int tileset_animate_value(const tileset_t *tileset, int value, uint32_t ticks_ms) {
    if (!tileset->animations || tileset->animation_count <= 0) return value;
    for (int i = 0; i < tileset->animation_count; ++i) {
        const TileAnimation *anim = &tileset->animations[i];
        if (anim->value != value) continue;
        int frame = (int)((ticks_ms / anim->frame_ms) % (uint32_t)anim->frame_count);
        return anim->frames[frame];
    }
    return value;
}

static int tileset_resolve_tile(const tileset_t *tileset, int value, uint32_t ticks_ms) {
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


void R_GridToScreen(const app_t *app, float gx, float gy, float *sx, float *sy) {
    *sx = gx * (float)app_cell_w(app) + app->cam.x;
    *sy = gy * (float)app_cell_h(app) + app->cam.y;
}

void R_MapToScreen(const app_t *app, const level_t *map, float gx, float gy,
                        float *sx, float *sy) {
    float screen_y = L_ScreenYF(map, gy);
    R_GridToScreen(app, gx, screen_y, sx, sy);
}

void R_MapPositionToScreen(const app_t *app, const level_t *map,
                           fixed3_t position, float *sx, float *sy) {
    fvec2_t planar = fixed3_xy_to_fvec2(position);
    R_MapToScreen(app, map, planar.x, planar.y, sx, sy);
    *sy -= fixed_to_float(position.z) * (float)app_cell_h(app);
}

static void screen_to_grid_point(const app_t *app, int sx, int sy, float *gx, float *gy) {
    if (gx) *gx = ((float)sx - app->cam.x) / (float)app_cell_w(app);
    if (gy) *gy = ((float)sy - app->cam.y) / (float)app_cell_h(app);
}

cell_t R_ScreenToGrid(const app_t *app, int sx, int sy) {
    return (cell_t){ (int)floorf(((float)sx - app->cam.x) / (float)app_cell_w(app)),
                   (int)floorf(((float)sy - app->cam.y) / (float)app_cell_h(app)) };
}

cell_t R_ScreenToMapGrid(const app_t *app, const level_t *map, int sx, int sy) {
    float gx = 0.0f, gy = 0.0f;
    screen_to_grid_point(app, sx, sy, &gx, &gy);
    gy = L_WorldYF(map, gy);
    return (cell_t){ (int)floorf(gx), (int)floorf(gy) };
}

static void screen_to_map_grid_point(const app_t *app, const level_t *map, int sx, int sy,
                                     float *gx, float *gy) {
    screen_to_grid_point(app, sx, sy, gx, gy);
    if (gy) *gy = L_WorldYF(map, *gy);
}

void R_RefreshViewport(app_t *app) {
    if (!app || !app->window || !app->renderer) return;
    int render_w = 0, render_h = 0;
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        render_w <= 0 || render_h <= 0) {
        SDL_GetWindowSize(app->window, &render_w, &render_h);
    }
    if (render_w > 0) app->win.w = render_w;
    if (render_h > 0) app->win.h = render_h;
}

void R_WindowToRenderPt(const app_t *app, int wx, int wy, int *rx, int *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (int)lroundf((float)wx * sx);
    if (ry) *ry = (int)lroundf((float)wy * sy);
}

void R_WindowToRenderDelta(const app_t *app, int wx, int wy, float *rx, float *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (float)wx * sx;
    if (ry) *ry = (float)wy * sy;
}

void R_DrawCell(app_t *app, int gx, int gy, SDL_Color color) {
    float sx, sy;
    R_GridToScreen(app, (float)gx, (float)gy, &sx, &sy);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    irect_t r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_RenderDrawRect(app->renderer, &r);
}

static void render_blocked_overlay(app_t *app, const level_t *map) {
    if (!app || !map || !map->blocked) return;
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (!map->blocked[L_Index(map, x, y)]) continue;
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)L_ScreenY(map, y), &sx, &sy);
            if (sx < -cell_w || sy < -cell_h ||
                sx > app->win.w + cell_w || sy > app->win.h + cell_h) {
                continue;
            }
            irect_t r = { (int)sx, (int)sy, cell_w, cell_h };
            SDL_SetRenderDrawColor(app->renderer, 230, 45, 40, 92);
            SDL_RenderFillRect(app->renderer, &r);
            SDL_SetRenderDrawColor(app->renderer, 255, 205, 64, 180);
            SDL_RenderDrawRect(app->renderer, &r);
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

static void render_tile_at_flipped(app_t *app, const tileset_t *tileset, int tile,
                                   irect_t src_part, irect_t dst_part, uint8_t transforms) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (tile < 0 || tile >= tileset->count) return;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (transforms & MAP_TILE_TRANSFORM_FLIP_X)
        flip = (SDL_RendererFlip)(flip | SDL_FLIP_HORIZONTAL);
    if (transforms & MAP_TILE_TRANSFORM_FLIP_Y)
        flip = (SDL_RendererFlip)(flip | SDL_FLIP_VERTICAL);
    if (tileset->indices) {
        const uint32_t *palette = tileset->palette;
        uint32_t colors[256];
        const tilepalettecycle_t *cycle = &tileset->palette_cycle;
        if (cycle->tiles && cycle->tiles[tile] && cycle->count > 1 && cycle->frame_ms) {
            unsigned phase = (app->ticks_ms / cycle->frame_ms) % cycle->count;
            memcpy(colors, palette, sizeof(colors));
            for (int i = 0; i < cycle->count; ++i)
                colors[cycle->indices[i]] = palette[cycle->indices[(i + phase) % cycle->count]];
            palette = colors;
        }
        isize2_t size = {tileset->tile_w, tileset->tile_h};
        const uint8_t *indices = tileset->indices + (size_t)tile * size.w * size.h;
        R_DrawIndexed(app->renderer, indices, size, palette, &src_part, &dst_part,
                       flip, (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND);
        return;
    }
    if (!tileset->texture) return;
    irect_t src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    if (flip == SDL_FLIP_NONE) {
        SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
    } else {
        SDL_RenderCopyEx(app->renderer, tileset->texture, &src, &dst_part, 0.0, NULL, flip);
    }
}

void R_DrawTile(app_t *app, const tileset_t *tileset, int tile, irect_t src_part, irect_t dst_part) {
    render_tile_at_flipped(app, tileset, tile, src_part, dst_part, 0);
}

void R_DrawLevel(app_t *app, const level_t *map, const tileset_t *tileset) {
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset;
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)L_ScreenY(map, y), &sx, &sy);
            if ((map->render_capabilities & MAP_RENDER_CAP_CELL_COLORS) && map->cell_colors) {
                if (sx < -cell_w || sy < -cell_h ||
                    sx > app->win.w + cell_w || sy > app->win.h + cell_h) {
                    continue;
                }
                uint32_t color = map->cell_colors[L_Index(map, x, y)];
                SDL_SetRenderDrawColor(app->renderer,
                                       (uint8_t)(color >> 16),
                                       (uint8_t)(color >> 8),
                                       (uint8_t)color,
                                       255);
                irect_t dst = { (int)sx, (int)sy, cell_w, cell_h };
                SDL_RenderFillRect(app->renderer, &dst);
                continue;
            }
            if (sx < -tile_w || sy < -tile_h ||
                sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                continue;
            }
            int idx = L_Index(map, x, y);
            int tile = map->tile_ids[idx];
            if ((map->render_capabilities & MAP_RENDER_CAP_ZERO_TILE_EMPTY) && tile == 0) continue;
            irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
            irect_t dst = {
                (int)sx,
                (int)(sy + draw_y_offset),
                tile_w,
                tile_h,
            };
            uint8_t base_flip =
                (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
                map->tile_transforms[0] ? map->tile_transforms[0][idx] : 0;
            render_tile_at_flipped(app, tileset, tile, src, dst, base_flip);
        }
    }

    for (int layer = 0;
         !(map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) &&
         layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS;
         ++layer) {
        if (!map->tile_overlays[layer]) continue;
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                R_GridToScreen(app, (float)x, (float)L_ScreenY(map, y), &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                    continue;
                }
                int idx = L_Index(map, x, y);
                int overlay = map->tile_overlays[layer][idx];
                if (overlay <= 0) continue;
                irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
                irect_t dst = {
                    (int)sx,
                    (int)(sy + draw_y_offset),
                    tile_w,
                    tile_h,
                };
                uint8_t overlay_flip =
                    (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
                    map->tile_transforms[layer + 1] ?
                    map->tile_transforms[layer + 1][idx] : 0;
                render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
            }
        }
    }

    if ((map->render_capabilities & MAP_RENDER_CAP_TERRAIN_TRANSITIONS) &&
        !(map->render_capabilities & MAP_RENDER_CAP_CELL_COLORS) &&
        map->render_transitions) {
        if (tileset->texture) {
            SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(tileset->texture, 255);
        }
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                R_GridToScreen(app, (float)x, (float)L_ScreenY(map, y), &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                    continue;
                }
                int dx = (int)sx;
                int dy = (int)(sy + draw_y_offset);
                map->render_transitions(app, map, tileset, x, y, dx, dy);
            }
        }
        if (tileset->texture) {
            SDL_SetTextureAlphaMod(tileset->texture, 255);
            SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_NONE);
        }
    }

    if (app->show_blocked) {
        render_blocked_overlay(app, map);
    }

}

void R_DrawGridOverlay(app_t *app, const level_t *map) {
    if (!app || !map || !app->show_grid) return;

    float left, bottom, right, top;
    R_MapToScreen(app, map, 0.0f, 0.0f, &left, &bottom);
    R_MapToScreen(app, map, (float)map->width, (float)map->height, &right, &top);

    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int x = 0; x <= map->width; ++x) {
        float sx, unused;
        R_MapToScreen(app, map, (float)x, 0.0f, &sx, &unused);
        if (sx < 0.0f || sx > (float)app->win.w) continue;
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(app->renderer, (int)lroundf(sx), (int)lroundf(top),
                          (int)lroundf(sx), (int)lroundf(bottom));
    }
    for (int y = 0; y <= map->height; ++y) {
        float unused, sy;
        R_MapToScreen(app, map, 0.0f, (float)y, &unused, &sy);
        if (sy < 0.0f || sy > (float)app->win.h) continue;
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(app->renderer, (int)lroundf(left), (int)lroundf(sy),
                          (int)lroundf(right), (int)lroundf(sy));
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

const spritesheet_t *R_CacheLookup(const spritecache_t *cache, const char *name) {
    if (!cache || !name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0)
            return cache->entries[i].alias ? cache->entries[i].alias : &cache->entries[i].sprite;
    }
    return NULL;
}

cachedsprite_t *R_CacheFind(spritecache_t *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i];
    }
    return NULL;
}

bool R_BindSprites(spritecache_t *cache, const gameinfo_t *game_info) {
    int count = game_info->sprite_count;
    const spritesheet_t **sprites = calloc((size_t)count, sizeof(*sprites));
    if (!sprites) return false;
    for (int i = 0; i < count; ++i)
        sprites[i] = R_CacheLookup(cache, game_info->sprnames[i]);
    free(cache->sprites);
    cache->sprites = sprites;
    cache->numsprites = count;
    return true;
}

const spritesheet_t *R_StateSprite(const spritecache_t *cache, const gameinfo_t *game_info,
                                   int sprite, const char *name) {
    if (!cache) return NULL;
    if (cache->sprites && sprite >= 0 && sprite < cache->numsprites)
        return cache->sprites[sprite];
    if (game_info && game_info->sprnames && sprite >= 0 && sprite < game_info->sprite_count)
        name = game_info->sprnames[sprite];
    return R_CacheLookup(cache, name);
}

bool R_AllocSpriteCells(spritesheet_t *sprite, int count) {
    if (count <= 0) return false;
    spritecell_t *cells = calloc((size_t)count, sizeof(*cells));
    spritelump_t *lumps = calloc((size_t)count, sizeof(*lumps));
    if (!cells || !lumps) {
        free(cells);
        free(lumps);
        return false;
    }
    sprite->cells = cells;
    sprite->lumps = lumps;
    sprite->numlumps = count;
    return true;
}

static void free_sprite_def(spritedef_t *def) {
    for (int i = 0; i < def->numframes; ++i) {
        spriteframe_t *frame = &def->spriteframes[i];
        for (int r = 0; r < frame->rotations; ++r)
            free(frame->directions[r].layers);
        free(frame->directions);
    }
    free(def->spriteframes);
    *def = (spritedef_t){0};
}

bool R_AllocSpriteDirections(spriteframe_t *frame, int rotations) {
    if (!frame || rotations < frame->rotations || rotations < 1 ||
        rotations > MAX_SPRITE_ROTATIONS) return false;
    spritedirection_t *directions = realloc(frame->directions,
        (size_t)rotations * sizeof(*directions));
    if (!directions) return false;
    memset(directions + frame->rotations, 0,
           (size_t)(rotations - frame->rotations) * sizeof(*directions));
    frame->directions = directions;
    frame->rotations = rotations;
    return true;
}

bool R_InitSpriteDef(spritesheet_t *sprite, int numframes, int rotations) {
    if (!sprite || numframes <= 0 || rotations <= 0 ||
        rotations > MAX_SPRITE_ROTATIONS) return false;
    spriteframe_t *frames = calloc((size_t)numframes, sizeof(*frames));
    if (!frames) return false;
    for (int i = 0; i < numframes; ++i) {
        if (!R_AllocSpriteDirections(&frames[i], rotations)) {
            for (int j = 0; j < i; ++j) free(frames[j].directions);
            free(frames);
            return false;
        }
    }
    free_sprite_def(&sprite->spritedef);
    sprite->spritedef = (spritedef_t){
        .numframes = numframes,
        .spriteframes = frames,
    };
    return true;
}

bool R_InstallSpriteLump(spritesheet_t *sprite, int frame, int rotation,
                         int lump, bool flip) {
    if (!sprite || !sprite->spritedef.spriteframes || frame < 0 ||
        frame >= sprite->spritedef.numframes || rotation < 0 ||
        rotation >= sprite->spritedef.spriteframes[frame].rotations || lump < 0 ||
        lump >= sprite->numlumps) return false;
    spriteframe_t *spriteframe = &sprite->spritedef.spriteframes[frame];
    spritedirection_t *direction = &spriteframe->directions[rotation];
    spritelayer_t *layers = calloc(2, sizeof(*layers));
    if (!layers) return false;
    free(direction->layers);
    direction->layers = layers;
    snprintf(direction->layers[0].sprite_name,
             sizeof(direction->layers[0].sprite_name), ".");
    direction->layers[0].lump = lump;
    direction->layers[0].intensity = 16;
    direction->layers[0].flags = flip ? RTS_FRAME_FLIP_X : 0;
    return true;
}

static int sprite_rotation_for_frame(const spritesheet_t *sprite, int frame,
                                     angle_t angle) {
    if (!sprite || frame < 0 || frame >= sprite->spritedef.numframes ||
        !sprite->spritedef.spriteframes) return -1;
    int rotations = sprite->spritedef.spriteframes[frame].rotations;
    return angle_to_direction(angle, rotations, ANG90, false);
}

static const spritesheet_t *sprite_layer_source(const spritecache_t *cache,
                                                const spritesheet_t *sprite,
                                                const char *name) {
    if (strcmp(name, ".") == 0) return sprite;
    const spritesheet_t *source = R_CacheLookup(cache, name);
    if (source) return source;
    char path[32];
    snprintf(path, sizeof(path), "SPRITES/%s.SPR", name);
    return R_CacheLookup(cache, path);
}

static const spritelayer_t *sprite_body_part(const spritesheet_t *sprite, int frame,
                                            int rotation) {
    if (!sprite || !sprite->spritedef.spriteframes || frame < 0 ||
        frame >= sprite->spritedef.numframes || rotation < 0 ||
        rotation >= MAX_SPRITE_ROTATIONS) return NULL;
    const spritelayer_t *parts =
        sprite->spritedef.spriteframes[frame].directions[rotation].layers;
    if (!parts) return NULL;
    const spritelayer_t *fallback = NULL;
    for (const spritelayer_t *part = parts; part->sprite_name[0] != '\0'; ++part) {
        if (strcmp(part->sprite_name, ".") != 0) continue;
        if (!fallback) fallback = part;
        if (part->layer == 1) return part;
    }
    return fallback;
}

static int sprite_lump_for_frame(const spritesheet_t *sprite, int frame,
                                 angle_t angle, bool *flip) {
    if (flip) *flip = false;
    int rotation = sprite_rotation_for_frame(sprite, frame, angle);
    const spritelayer_t *part = sprite_body_part(sprite, frame, rotation);
    if (!part) return -1;
    if (flip) *flip = (part->flags & RTS_FRAME_FLIP_X) != 0;
    return part->lump;
}


static int decoration_animation_step(const app_t *app, const mapdecoration_t *dec) {
    int total_ms = 0;
    for (int i = 0; i < dec->animation_frame_count; ++i)
        total_ms += dec->animation_frames[i].duration_ms;
    if (total_ms <= 0) return 0;

    int elapsed_ms = (int)(app->ticks_ms % (uint32_t)total_ms);
    for (int i = 0; i < dec->animation_frame_count; ++i) {
        int duration_ms = dec->animation_frames[i].duration_ms;
        if (elapsed_ms < duration_ms) return i;
        elapsed_ms -= duration_ms;
    }
    return dec->animation_frame_count - 1;
}

static int decoration_sprite_frame(app_t *app, const mapdecoration_t *dec, const spritesheet_t *sprite,
                                   int frame_index) {
    if (frame_index >= sprite->numlumps) {
        int lump = sprite_lump_for_frame(sprite, frame_index, dec->angle, NULL);
        return lump >= 0 ? lump : 0;
    }
    if (frame_index >= 0) return frame_index;
    if (frame_index < 0 && dec->animation_frame_count > 0) {
        int step = decoration_animation_step(app, dec);
        int authored_frame = dec->animation_frames[step].sprite_frame;
        if (authored_frame >= 0 && authored_frame < sprite->numlumps) return authored_frame;
    }
    if (frame_index < 0) {
        uint32_t frame_ms = dec->frame_interval_ms > 0 ?
            (uint32_t)dec->frame_interval_ms : 250u;
        return (int)((app->ticks_ms / frame_ms) % (uint32_t)sprite->numlumps);
    }
    return 0;
}

static uint8_t fin_intensity_color_mod(int intensity) {
    if (intensity <= 0) intensity = 16;
    return (uint8_t)clamp255((intensity * 255 + 8) / 16);
}

static SDL_Color sprite_color(int intensity) {
    uint8_t value = fin_intensity_color_mod(intensity);
    return (SDL_Color){ value, value, value, 255 };
}

static uint8_t nearest_palette_index(uint32_t rgba, const uint32_t palette[256]) {
    int r = (int)((rgba >> 16) & 0xff);
    int g = (int)((rgba >> 8) & 0xff);
    int b = (int)(rgba & 0xff);
    int best_index = 1;
    int best_distance = INT32_MAX;
    for (int i = 1; i < 256; ++i) {
        int dr = r - (int)((palette[i] >> 16) & 0xff);
        int dg = g - (int)((palette[i] >> 8) & 0xff);
        int db = b - (int)(palette[i] & 0xff);
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
            if (distance == 0) break;
        }
    }
    return (uint8_t)best_index;
}

static uint8_t cached_palette_index(uint32_t rgba, const uint32_t palette[256],
                                    uint32_t matches[4096]) {
    uint32_t rgb = rgba & 0x00ffffffu;
    uint32_t *match = &matches[(rgb * 2654435761u) >> 20];
    uint8_t index = (uint8_t)*match;
    if (!index || (*match >> 8) != rgb) {
        index = nearest_palette_index(rgb, palette);
        *match = (rgb << 8) | index;
    }
    return index;
}

/* DC.EXE 0x45c7b0/0x45cc04 project the source silhouette; 0x45ba5a/
 * 0x463bc0 repeat rows using an 8-bit accumulator and remap the destination.
 * Keep the indexed source image as the owner, like Doom's colormap drawing. */
bool R_RenderSpriteShadow(app_t *app, const spritesheet_t *sprite, int frame,
                          irect_t ground_dst, uint32_t flags) {
    if (!app || !sprite || !sprite->shadowmap || !sprite->lumps ||
        frame < 0 || frame >= sprite->numlumps || !sprite->lumps[frame].indices)
        return false;
    irect_t source = sprite->cells[frame].rect;
    if (source.w <= 0 || source.h <= 0) return false;
    int height = source.h + source.h * 40 / 256;
    int bottom = ground_dst.y + source.h;
    int top = bottom - height;
    int shear = (bottom < height ? bottom : height) >> 1;
    bool flip = (flags & RTS_FRAME_FLIP_X) != 0;
    /* The mirrored native span starts at X+width and writes backwards. */
    int left = ground_dst.x - shear + (flip ? 1 : 0);
    int source_y = top < 0 ? -top * 256 / 296 : 0;
    int rows = top < 0 ? bottom : height;
    if (top < 0) top = 0;
    irect_t bounds = { left, top, source.w + height / 2, rows };
    irect_t window = { 0, 0, app->win.w, app->win.h }, clip;
    if (!SDL_IntersectRect(&bounds, &window, &clip)) return true;
    size_t count = (size_t)clip.w * clip.h;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) return false;
    if (SDL_RenderReadPixels(app->renderer, &clip, SDL_PIXELFORMAT_ARGB8888,
                             pixels, clip.w * (int)sizeof(*pixels)) != 0) {
        free(pixels);
        return false;
    }
    uint32_t palette_matches[4096] = {0};
    unsigned stretch = 0;
    bool repeated = false;
    for (int row = 0; row < rows && top + row < clip.y + clip.h && source_y < source.h; ++row) {
        int y = top + row;
        int x_start = left + row / 2;
        for (int x = 0; x < source.w; ++x) {
            int dst_x = x_start + (flip ? source.w - 1 - x : x);
            if (dst_x < clip.x || dst_x >= clip.x + clip.w || y < clip.y ||
                !sprite->lumps[frame].indices[(size_t)source_y * source.w + x]) continue;
            uint32_t *pixel = &pixels[(size_t)(y - clip.y) * clip.w + dst_x - clip.x];
            uint8_t index = (*pixel & 0x00ffffffu) == (sprite->palette[0] & 0x00ffffffu) ? 0 :
                cached_palette_index(*pixel, sprite->palette, palette_matches);
            *pixel = sprite->palette[sprite->shadowmap[index]] | 0xff000000u;
        }
        if (!repeated) {
            stretch += 40;
            repeated = stretch >= 256;
            stretch &= 255;
            if (repeated) continue;
        }
        repeated = false;
        ++source_y;
    }
    SDL_Texture *composite = I_CreateTexture(app->renderer, pixels, clip.w, clip.h, false);
    free(pixels);
    if (!composite) return false;
    SDL_RenderCopy(app->renderer, composite, NULL, &clip);
    SDL_DestroyTexture(composite);
    return true;
}

bool R_RenderIndexedBlend(app_t *app, const spritesheet_t *sprite, int frame,
                          irect_t dst, uint32_t flags, int selector) {
    if (!app || !sprite || !sprite->indexed || !sprite->indexed_blend_table ||
        selector != sprite->indexed_blend_selector || !sprite->lumps ||
        frame < 0 || frame >= sprite->numlumps || !sprite->lumps[frame].indices)
        return false;

    irect_t clip = dst;
    if (clip.x < 0) { clip.w += clip.x; clip.x = 0; }
    if (clip.y < 0) { clip.h += clip.y; clip.y = 0; }
    if (clip.x + clip.w > app->win.w) clip.w = app->win.w - clip.x;
    if (clip.y + clip.h > app->win.h) clip.h = app->win.h - clip.y;
    if (clip.w <= 0 || clip.h <= 0) return true;

    size_t pixel_count = (size_t)clip.w * (size_t)clip.h;
    uint32_t *pixels = malloc(pixel_count * sizeof(*pixels));
    if (!pixels) return false;
    SDL_Rect read_rect = { clip.x, clip.y, clip.w, clip.h };
    if (SDL_RenderReadPixels(app->renderer, &read_rect, SDL_PIXELFORMAT_ARGB8888,
                             pixels, clip.w * (int)sizeof(*pixels)) != 0) {
        free(pixels);
        return false;
    }

    /* Keep exact RGB matches for this palette/draw. The low byte holds a
     * nonzero palette index, so zero marks an empty slot. Hash collisions
     * only cause another search; they never approximate the color. */
    uint32_t palette_matches[4096] = {0};
    irect_t source = sprite->cells[frame].rect;
    for (int y = 0; y < clip.h; ++y) {
        int source_y = clip.y - dst.y + y;
        for (int x = 0; x < clip.w; ++x) {
            int local_x = clip.x - dst.x + x;
            if ((flags & RTS_FRAME_FLIP_X) != 0) local_x = source.w - 1 - local_x;
            uint8_t source_index = sprite->lumps[frame].indices[
                (size_t)source_y * (size_t)source.w + (size_t)local_x];
            if (source_index == 0) continue;
            size_t pixel = (size_t)y * (size_t)clip.w + (size_t)x;
            uint8_t destination_index = cached_palette_index(
                pixels[pixel], sprite->palette, palette_matches);
            uint8_t result_index = sprite->indexed_blend_table[
                ((size_t)source_index << 8) | destination_index];
            pixels[pixel] = sprite->palette[result_index];
        }
    }

    SDL_Texture *composite = I_CreateTexture(app->renderer, pixels, clip.w, clip.h, false);
    free(pixels);
    if (!composite) return false;
    SDL_RenderCopy(app->renderer, composite, NULL, &read_rect);
    SDL_DestroyTexture(composite);
    return true;
}

static SDL_Point sprite_frame_raw_displacement(const spritesheet_t *sprite, int frame);
static SDL_Point sprite_ground_point(const spritesheet_t *sprite, int frame);

static void render_decoration_sprite(app_t *app, const level_t *map,
                                     const mapdecoration_t *dec, const spritesheet_t *sprite,
                                     int frame_index, uint32_t render_flags,
                                     int render_selector, int anchor_frame_index) {
    if (!sprite || sprite->spritedef.numframes <= 0) return;

    float sx, sy;
    fvec2_t anchor = { (float)dec->cell.x, (float)dec->cell.y };
    isize2_t footprint = {
        dec->footprint.w > 0 ? dec->footprint.w : 1,
        dec->footprint.h > 0 ? dec->footprint.h : 1,
    };
    R_MapToScreen(app, map, anchor.x, anchor.y, &sx, &sy);

    int frame = decoration_sprite_frame(app, dec, sprite, frame_index);
    int anchor_frame = decoration_sprite_frame(app, dec, sprite, anchor_frame_index);
    irect_t frame_rect = sprite_frame_rect(sprite, frame);
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;

    irect_t dst;
    if (dec->has_sprite_pivot) {
        /* The plugin-authored point is in the full frame canvas, not in the
           visible-pixel bounds.  All layers of a composite therefore attach
           to exactly the same world point. */
        if (dec->center_anchor) {
            anchor = fvec2_add(anchor, fvec2_scale(
                (fvec2_t){ (float)footprint.w, (float)footprint.h }, 0.5f));
            R_MapToScreen(app, map, anchor.x, anchor.y, &sx, &sy);
        }
        ivec2_t pivot = dec->sprite_pivot;
        if (frame_index < 0 && dec->animation_frame_count > 0)
            pivot = dec->animation_frames[decoration_animation_step(app, dec)].sprite_pivot;
        dst = (irect_t){
            (int)lroundf(sx) - pivot.x,
            (int)lroundf(sy) - pivot.y,
            sprite_w,
            sprite_h,
        };
    } else if (dec->center_anchor) {
        anchor = fvec2_add(anchor, fvec2_scale(
            (fvec2_t){ (float)footprint.w, (float)footprint.h }, 0.5f));
        R_MapToScreen(app, map, anchor.x, anchor.y, &sx, &sy);
        SDL_Point ground;
        if (sprite->lumps) {
            ground = sprite_ground_point(sprite, anchor_frame);
        } else {
            /* Preserve the legacy decoration anchor for formats without an
               authored ground point (notably Dark Colony FIN sprites). */
            irect_t anchor_rect = sprite_frame_rect(sprite, anchor_frame);
            irect_t bounds = sprite_visible_bounds(sprite, anchor_frame);
            ground = (SDL_Point){ anchor_rect.w / 2, bounds.y + bounds.h };
        }
        dst = (irect_t){
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
        dst = (irect_t){
            (int)(sx + (float)(footprint.w * app_cell_w(app) - sprite_w) * 0.5f),
            (int)(sy + (float)(footprint.h * app_cell_h(app) - sprite_h)),
            sprite_w,
            sprite_h,
        };
    }
    if (dst.x > app->win.w || dst.y > app->win.h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        return;
    }
    if ((render_flags & RTS_FRAME_BLINK) != 0 && ((app->ticks_ms / 250u) % 2u) == 0u) {
        return;
    }
    if (R_RenderIndexedBlend(app, sprite, frame, dst, render_flags, render_selector)) return;
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    R_DrawSprite(app->renderer, sprite, frame, dec->render_remap, NULL, &dst,
                 flip, sprite_color(16), SDL_BLENDMODE_BLEND);
}

static void render_decoration(app_t *app, const level_t *map,
                              const mapdecoration_t *dec, const spritecache_t *cache) {
    if (dec->hidden) return;
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->shadow_name),
                             dec->frame_index, dec->render_flags, dec->render_selector,
                             dec->frame_index);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite_name),
                             dec->frame_index, dec->render_flags, dec->render_selector,
                             dec->frame_index);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite2_name),
                             dec->frame2_index, dec->render2_flags, dec->render2_selector,
                             dec->frame_index);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite3_name),
                             dec->frame3_index, dec->render3_flags, dec->render3_selector,
                             dec->frame_index);
}

void R_DrawDecorations(app_t *app, const level_t *map, const spritecache_t *cache) {
    for (int i = 0; i < map->decoration_count; ++i) {
        render_decoration(app, map, &map->decorations[i], cache);
    }
}

static float unit_pick_radius_px(const app_t *app, const mobj_t *unit) {
    float cell = ((float)app_cell_w(app) + (float)app_cell_h(app)) * 0.5f;
    float radius = P_MobjRadius(unit) * cell;
    float min_radius = 12.0f;
    return radius < min_radius ? min_radius : radius;
}

static bool circle_intersects_rect(fvec2_t center, float radius, irect_t r) {
    float nearest_x = center.x;
    float nearest_y = center.y;
    if (nearest_x < (float)r.x) nearest_x = (float)r.x;
    if (nearest_x > (float)(r.x + r.w)) nearest_x = (float)(r.x + r.w);
    if (nearest_y < (float)r.y) nearest_y = (float)r.y;
    if (nearest_y > (float)(r.y + r.h)) nearest_y = (float)(r.y + r.h);
    float dx = center.x - nearest_x;
    float dy = center.y - nearest_y;
    return dx * dx + dy * dy <= radius * radius;
}

static irect_t sprite_visible_bounds(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->lumps && frame >= 0 && frame < sprite->numlumps) {
        irect_t r = sprite->cells[frame].bounds;
        if (r.w > 0 && r.h > 0) return r;
    }
    return (irect_t){ 0, 0,
                      sprite ? sprite->frame_size.w : 1,
                      sprite ? sprite->frame_size.h : 1 };
}

static irect_t sprite_frame_rect(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->lumps && frame >= 0 && frame < sprite->numlumps &&
        sprite->cells[frame].rect.w > 0 && sprite->cells[frame].rect.h > 0) {
        return sprite->cells[frame].rect;
    }
    return (irect_t){ 0, 0,
                      sprite ? sprite->frame_size.w : 1,
                      sprite ? sprite->frame_size.h : 1 };
}

static SDL_Point sprite_frame_raw_displacement(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->lumps && frame >= 0 && frame < sprite->numlumps) {
        return (SDL_Point){ sprite->cells[frame].displacement.x,
                            sprite->cells[frame].displacement.y };
    }
    return (SDL_Point){ 0, 0 };
}


static SDL_Point sprite_ground_point(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->lumps && frame >= 0 && frame < sprite->numlumps) {
        return (SDL_Point){ sprite->cells[frame].ground_point.x,
                            sprite->cells[frame].ground_point.y };
    }
    irect_t bounds = sprite_visible_bounds(sprite, frame);
    return (SDL_Point){ bounds.x + bounds.w / 2, bounds.y + bounds.h };
}

static const spritesheet_t *unit_sprite_sheet_for_view(const mobj_t *unit,
                                                     const spritesheet_t *fallback_sprite,
                                                     const spritecache_t *cache,
                                                     const gameinfo_t *game_info) {
    if (!unit) return NULL;
    const spritesheet_t *sprite = R_StateSprite(cache, game_info,
                                                unit->core.sprite_id, unit->core.sprite_name);
    return sprite ? sprite : fallback_sprite;
}

static int unit_frame_for_view(const spritesheet_t *sprite, const mobj_t *unit,
                               const gameinfo_t *game_info, uint32_t ticks,
                               bool *flip) {
    (void)ticks;
    bool has_state_frames = game_info && game_info->states && game_info->state_count > 0;
    int logical_frame = has_state_frames ? unit->core.frame : 0;
    int lump = sprite_lump_for_frame(sprite, logical_frame, unit->core.angle, flip);
    return lump >= 0 ? lump : 0;
}

static bool unit_screen_rect_for_view(const app_t *app, const level_t *map, const mobj_t *unit,
                                      const spritesheet_t *fallback_sprite,
                                      const spritecache_t *cache,
                                      const gameinfo_t *game_info, uint32_t ticks,
                                      irect_t *dst_out, irect_t *visible_out,
                                      float *sx_out, float *sy_out,
                                      int *frame_out, bool *flip_out,
                                      const spritesheet_t **sprite_out) {
    if (!app || !unit) return false;
    float sx = 0.0f, sy = 0.0f;
    R_MapPositionToScreen(app, map, unit->core.position, &sx, &sy);
    const spritesheet_t *sprite = unit_sprite_sheet_for_view(unit, fallback_sprite, cache, game_info);
    if (!sprite || !sprite->lumps || sprite->numlumps <= 0) {
        float radius = unit_pick_radius_px(app, unit);
        irect_t fallback = {
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

    bool frame_flip = false;
    int frame = unit_frame_for_view(sprite, unit, game_info, ticks, &frame_flip);
    irect_t frame_rect = sprite_frame_rect(sprite, frame);
    irect_t bounds = sprite_visible_bounds(sprite, frame);
    uint32_t render_flags = game_info ? unit->core.render_flags : 0;
    if (frame_flip) render_flags ^= RTS_FRAME_FLIP_X;
    if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
        bounds.x = frame_rect.w - bounds.x - bounds.w;
    }
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;
    int body_offset_x = 0;
    int body_offset_y = 0;
    irect_t dst;
    if (game_info && game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
        SDL_Point pivot = sprite_ground_point(sprite, frame);
        if ((render_flags & RTS_FRAME_FLIP_X) != 0)
            pivot.x = frame_rect.w - pivot.x;
        dst = (irect_t){
            (int)lroundf(sx) + body_offset_x - pivot.x,
            (int)lroundf(sy) + body_offset_y - pivot.y,
            sprite_w,
            sprite_h,
        };
    } else {
        SDL_Point ground = sprite_ground_point(sprite, frame);
        if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
            ground.x = frame_rect.w - ground.x;
        }
        dst = (irect_t){
            (int)lroundf(sx - (float)ground.x) + body_offset_x,
            (int)lroundf(sy - (float)ground.y) + body_offset_y,
            sprite_w,
            sprite_h,
        };
    }
    dst.x += unit->core.render_offset.x;
    dst.y += unit->core.render_offset.y;
    irect_t visible = {
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
    if (flip_out) *flip_out = (render_flags & RTS_FRAME_FLIP_X) != 0;
    if (sprite_out) *sprite_out = sprite;
    return true;
}

int R_PickUnit(const app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
                        const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                        const gameinfo_t *game_info, int x, int y, int owner_filter) {
    int best = -1;
    float best_score = 1000000000.0f;
    for (int i = unit_count - 1; i >= 0; --i) {
        const mobj_t *unit = units[i];
        if (!P_VisibleToPlayer(unit) || unit->hp <= 0 ||
            (unit->traits & MF_SELECTABLE) == 0) continue;
        if (owner_filter >= 0 && unit->owner != owner_filter) continue;
        irect_t visible;
        float sx = 0.0f, sy = 0.0f;
        unit_screen_rect_for_view(app, map, unit, fallback_sprite, cache, game_info, app->ticks_ms,
                                  NULL, &visible, &sx, &sy, NULL, NULL, NULL);
        if (!irect_contains(visible, (ivec2_t){ x, y })) continue;
        float dx = (float)x - sx;
        float dy = (float)y - sy;
        float score = dx * dx + dy * dy;
        if (score >= best_score) continue;
        best_score = score;
        best = i;
    }
    return best;
}

static int selection_health_bucket(const mobj_t *u) {
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

static void draw_selection_circle(app_t *app, const mobj_t *u, int cx, int cy, int radius) {
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

static void draw_selection_brackets(app_t *app, const mobj_t *u, const irect_t *visible) {
    if (!app || !app->renderer || !u || !visible || visible->w <= 0 || visible->h <= 0) return;
    irect_t box = {
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
    SDL_RenderFillRect(app->renderer, &(irect_t){ bar_x - 1, bar_y - 1, bar_w + 2, 4 });
    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, 255);
    SDL_RenderFillRect(app->renderer, &(irect_t){ bar_x, bar_y, fill_w, 2 });
}

static void draw_selection_triangle(app_t *app, const mobj_t *u, const irect_t *visible) {
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

static void render_centered_mobj(app_t *app, const level_t *map, const mobj_t *mobj,
                                const spritecache_t *cache, const gameinfo_t *game_info);

static void render_unit_sprite(app_t *app, const level_t *map,
                               const mobj_t *u, const spritesheet_t *fallback_sprite,
                               const spritecache_t *cache, const gameinfo_t *game_info,
                               uint32_t ticks) {
    if (!u || !P_VisibleToPlayer(u) || (u->traits & MF_RENDERABLE) == 0) return;
    if ((u->traits & MF_NOBLOCKMAP) &&
        (!game_info || game_info->state_coord_mode != RTS_STATE_COORDS_FIN_TOP_LEFT)) {
        render_centered_mobj(app, map, u, cache, game_info);
        return;
    }
    const spritesheet_t *sprite = unit_sprite_sheet_for_view(u, fallback_sprite, cache, game_info);
    if (!sprite || sprite->spritedef.numframes <= 0) return;

    float sx = 0.0f, sy = 0.0f;
    int frame = 0;
    bool frame_flip = false;
    irect_t dst;
    irect_t visible;
    unit_screen_rect_for_view(app, map, u, fallback_sprite, cache, game_info, ticks,
                              &dst, &visible, &sx, &sy, &frame, &frame_flip, &sprite);
    uint32_t render_flags = game_info ? u->core.render_flags : 0;
    if (frame_flip) render_flags |= RTS_FRAME_FLIP_X;
    const spritesheet_t *shadow = R_CacheLookup(
        cache, u->info && u->info->shadow_name ? u->info->shadow_name : "");
    /* Some Dark Reign unit definitions repeat the body RSPR in
       SetShadowImage.  The original treats its shadow data specially; our
       cache resolves that name to the already-loaded colour body sheet, so
       drawing it here would create a second vehicle that mirrors every move. */
    if (shadow && shadow != sprite && shadow->lumps && shadow->numlumps > 0) {
        int shadow_frame = frame < shadow->numlumps ? frame : 0;
        irect_t shadow_rect = sprite_frame_rect(shadow, shadow_frame);
        irect_t shadow_dst = { dst.x, dst.y, shadow_rect.w, shadow_rect.h };
        R_DrawSprite(app->renderer, shadow, shadow_frame, -1, NULL, &shadow_dst,
                     SDL_FLIP_NONE, sprite_color(16), SDL_BLENDMODE_BLEND);
    }
    int logical_frame = game_info && game_info->states && game_info->state_count > 0 ?
        u->core.frame : 0;
    int rotation = sprite_rotation_for_frame(sprite, logical_frame, u->core.angle);
    const spriteframe_t *spriteframe = rotation >= 0 ?
        &sprite->spritedef.spriteframes[logical_frame] : NULL;
    const spritelayer_t *parts = spriteframe ?
        spriteframe->directions[rotation].layers : NULL;
    if (parts) {
        for (const spritelayer_t *part = parts; part->sprite_name[0] != '\0'; ++part) {
            const spritesheet_t *source = sprite_layer_source(
                cache, sprite, part->sprite_name);
            if (!source || !source->lumps || part->lump >= source->numlumps)
                continue;
            irect_t source_rect = sprite_frame_rect(source, part->lump);
            SDL_Point displacement = sprite_frame_raw_displacement(source, part->lump);
            uint32_t part_flags = part->flags;
            irect_t part_dst;
            if (game_info && game_info->state_coord_mode == RTS_STATE_COORDS_GROUND_OFFSET) {
                /* DR/KKnD: dst already has ground_point applied; use it as canvas origin. */
                part_dst = (irect_t){
                    dst.x + part->offset.x +
                        ((part_flags & RTS_FRAME_FLIP_X) ? 0 : displacement.x),
                    dst.y + part->offset.y,
                    source_rect.w,
                    source_rect.h,
                };
            } else {
                part_dst = (irect_t){
                    (int)lroundf(sx) + u->core.render_offset.x + part->offset.x +
                        ((part_flags & RTS_FRAME_FLIP_X) ? 0 : displacement.x),
                    (int)lroundf(sy) + u->core.render_offset.y + part->offset.y - source_rect.h,
                    source_rect.w,
                    source_rect.h,
                };
            }
            if (part->layer == 1 || part->layer == 2) {
                irect_t ground_dst = part_dst;
                ground_dst.y += (int)lroundf(fixed_to_float(u->core.position.z) * app_cell_h(app));
                R_RenderSpriteShadow(app, source, part->lump, ground_dst, part_flags);
            }
            if (source->shadowmap && part->layer == 2) continue;
            if (R_RenderIndexedBlend(app, source, part->lump, part_dst,
                                     part_flags, part->layer)) continue;
            /* DC.EXE queues object team color separately from FIN remap,
             * which selects a clipping path, not a palette translation. */
            SDL_Color color = sprite_color(part->intensity);
            SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
            if (part->layer == 3) {
                blend = SDL_BLENDMODE_ADD;
                color.g = (uint8_t)((color.g * 236 + 127) / 255);
                color.b = (uint8_t)((color.b * 72 + 127) / 255);
                color.a = 230;
            }
            SDL_RendererFlip part_flip = (part_flags & RTS_FRAME_FLIP_X) ?
                SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            R_DrawSprite(app->renderer, source, part->lump, u->team, NULL,
                         &part_dst, part_flip, color, blend);
        }
    } else {
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    R_DrawSprite(app->renderer, sprite, frame, u->core.render_remap, NULL, &dst,
                 flip, sprite_color(u->core.render_intensity), SDL_BLENDMODE_BLEND);
    }
    if (game_info && game_info->draw_overlays) return;
    if (P_MobjIsSelected(u) && (u->traits & MF_SELECTABLE)) {
        if (game_info && game_info->selection_marker.style == SELECTION_STYLE_CIRCLE) {
            int radius = (int)(unit_pick_radius_px(app, u) * 0.85f);
            draw_selection_circle(app, u, (int)sx, (int)sy, radius);
        } else if (game_info && game_info->selection_marker.style == SELECTION_STYLE_BRACKETS)
            draw_selection_brackets(app, u, &visible);
        else
            draw_selection_triangle(app, u, &visible);
    }
    if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp &&
        (!game_info || game_info->selection_marker.style != SELECTION_STYLE_BRACKETS ||
         !P_MobjIsSelected(u))) {
        int bar_w = dst.w / 2;
        int bar_h = 2;
        int bx = (int)(sx - bar_w / 2);
        int by = visible.y - bar_h - 4;
        irect_t back = { bx, by, bar_w, bar_h };
        irect_t fill = { bx, by, (bar_w * u->hp) / u->max_hp, bar_h };
        SDL_SetRenderDrawColor(app->renderer, 40, 20, 20, 220);
        SDL_RenderFillRect(app->renderer, &back);
        SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 230);
        SDL_RenderFillRect(app->renderer, &fill);
    }
}

static void render_unit_overlays(app_t *app, const level_t *map, mobj_t *const *units,
                                 int unit_count, const spritecache_t *cache,
                                 const gameinfo_t *game_info) {
    if (!game_info || !game_info->draw_overlays) return;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = units[i];
        if (!unit || !P_VisibleToPlayer(unit) || !(unit->traits & MF_RENDERABLE)) continue;
        unitoverlaycontext_t ctx = {
            .app = app, .unit = unit, .cache = cache,
            .game_info = game_info,
        };
        R_MapPositionToScreen(app, map, unit->core.position, &ctx.anchor.x, &ctx.anchor.y);
        game_info->draw_overlays(&ctx);
    }
}

void R_DrawThings(app_t *app, mobj_t *const *units, int unit_count, const spritesheet_t *fallback_sprite,
                  const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i)
        render_unit_sprite(app, &level, units[i], fallback_sprite, cache, game_info, ticks);
    render_unit_overlays(app, &level, units, unit_count, cache, game_info);
}

static int compare_draw_commands(const void *a, const void *b) {
    const drawcommand_t *ia = a;
    const drawcommand_t *ib = b;
    /* Elevated objects cover ground objects regardless of their ground Y. */
    if (ia->sort_z < ib->sort_z) return -1;
    if (ia->sort_z > ib->sort_z) return 1;
    if (ia->sort_y < ib->sort_y) return -1;
    if (ia->sort_y > ib->sort_y) return 1;
    if (ia->layer != ib->layer) return (int)ia->layer - (int)ib->layer;
    if (ia->kind != ib->kind) return (int)ia->kind - (int)ib->kind;
    return ia->stable_index - ib->stable_index;
}

static void render_overlay_tile_item(app_t *app, const level_t *map, const tileset_t *tileset,
                                     int x, int y, int layer) {
    if (!app || !map || !tileset || layer < 0 || layer >= map->tile_overlay_count ||
        layer >= MAX_TILE_OVERLAYS || !map->tile_overlays[layer]) {
        return;
    }
    int idx = L_Index(map, x, y);
    int overlay = map->tile_overlays[layer][idx];
    if (overlay <= 0) return;

    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset;
    float sx, sy;
    R_GridToScreen(app, (float)x, (float)L_ScreenY(map, y), &sx, &sy);
    if (sx < -tile_w || sy < -tile_h ||
        sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
        return;
    }
    irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
    irect_t dst = {
        (int)sx,
        (int)(sy + draw_y_offset),
        tile_w,
        tile_h,
    };
    uint8_t overlay_flip =
        (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
        map->tile_transforms[layer + 1] ? map->tile_transforms[layer + 1][idx] : 0;
    render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
}

void R_RenderPlayerView(app_t *app, const level_t *map, const tileset_t *tileset,
                          mobj_t *const *units, int unit_count, const spritesheet_t *fallback_sprite,
                          const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks) {
    if (!app || !map) return;
    int overlay_count = 0;
    if (map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][L_Index(map, x, y)] > 0) overlay_count++;
                }
            }
        }
    }

    int decoration_count = map->decoration_count > 0 ? map->decoration_count : 0;
    int total = overlay_count + decoration_count + (unit_count > 0 ? unit_count : 0);
    if (total <= 0) return;
    drawcommand_t *commands = malloc((size_t)total * sizeof(*commands));
    if (!commands) {
        R_DrawDecorations(app, map, cache);
        for (int i = 0; i < unit_count; ++i) {
            render_unit_sprite(app, map, units[i], fallback_sprite, cache, game_info, ticks);
        }
        render_unit_overlays(app, map, units, unit_count, cache, game_info);
        return;
    }

    int count = 0;
    if (map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][L_Index(map, x, y)] <= 0) continue;
                    commands[count++] = (drawcommand_t){
                        .kind = DRAW_COMMAND_TILE_OVERLAY,
                        .layer = RENDER_LAYER_TERRAIN_OVERLAY,
                        .sort_y = L_ScreenY(map, y) + 1.0f + (float)layer * 0.001f,
                        .stable_index = L_Index(map, x, y),
                        .ref.tile_overlay = { .x = x, .y = y, .layer = layer },
                    };
                }
            }
        }
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        int footprint_height = dec->footprint.h > 0 ? dec->footprint.h : 1;
        float sort_y = dec->has_sprite_pivot ?
            (float)dec->cell.y + (float)footprint_height :
            dec->center_anchor ?
            (float)dec->cell.y + 0.5f :
            (float)dec->cell.y + (float)footprint_height;
        sort_y = L_ScreenYF(map, sort_y);
        commands[count++] = (drawcommand_t){
            .kind = DRAW_COMMAND_DECORATION,
            .layer = RENDER_LAYER_DECORATION,
            .sort_y = sort_y,
            .stable_index = i,
            .ref.decoration = dec,
        };
    }
    for (int i = 0; i < unit_count; ++i) {
        if (!P_VisibleToPlayer(units[i])) continue;
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        commands[count++] = (drawcommand_t){
            .kind = DRAW_COMMAND_UNIT,
            .layer = RENDER_LAYER_UNIT,
            .sort_z = units[i]->core.position.z,
            .sort_y = L_ScreenYF(map, position.y),
            .stable_index = i,
            .ref.unit = units[i],
        };
    }

    qsort(commands, (size_t)count, sizeof(*commands), compare_draw_commands);
    for (int i = 0; i < count; ++i) {
        drawcommand_t *command = &commands[i];
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
    render_unit_overlays(app, map, units, unit_count, cache, game_info);
}

static void render_centered_mobj(app_t *app, const level_t *map, const mobj_t *effect,
                                const spritecache_t *cache, const gameinfo_t *game_info) {
    int i = (int)effect->id;
    const spritesheet_t *sprite = R_StateSprite(cache, game_info,
                                                (effect->core.state_id > 0) ? effect->core.sprite_id : -1, effect->core.sprite_name);
    if (!sprite || !sprite->lumps || sprite->numlumps <= 0) {
        debug_effects_log("render skip slot=%d sprite=%s reason=missing-cache",
                          i, effect->core.sprite_name);
        return;
    }

    float sx, sy;
    fvec2_t position = fixed3_xy_to_fvec2(effect->core.position);
    R_MapPositionToScreen(app, map, effect->core.position, &sx, &sy);
    int frame = sprite_lump_for_frame(sprite, effect->core.frame, effect->core.angle, NULL);
    if (frame < 0 || frame >= sprite->numlumps) frame = 0;
    irect_t frame_rect = sprite_frame_rect(sprite, frame);
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;
    irect_t dst = { (int)(sx - sprite_w / 2) + effect->core.render_offset.x,
        (int)(sy - sprite_h / 2) + effect->core.render_offset.y, sprite_w, sprite_h };
    if (dst.x > app->win.w || dst.y > app->win.h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        debug_effects_log("render skip slot=%d sprite=%s frame_count=%d pos=%.2f,%.2f dst=%d,%d,%d,%d reason=offscreen",
                          i, effect->core.sprite_name,
                          sprite->numlumps, position.x, position.y,
                          dst.x, dst.y, dst.w, dst.h);
        return;
    }
    SDL_RendererFlip flip = (effect->core.render_flags & RTS_FRAME_FLIP_X) ?
        SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    if (R_RenderIndexedBlend(app, sprite, frame, dst, effect->core.render_flags,
                             0)) return;
    R_DrawSprite(app->renderer, sprite, frame, effect->core.render_remap, NULL, &dst,
                 flip, sprite_color(effect->core.render_intensity), SDL_BLENDMODE_BLEND);
}

static void order_selected_at(app_t *app, const level_t *map,
                              mobj_t *const *units, int unit_count,
                              const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                              const gameinfo_t *game_info, ivec2_t mouse) {
    fvec2_t goal;
    screen_to_map_grid_point(app, map, mouse.x, mouse.y, &goal.x, &goal.y);
    int target = R_PickUnit(app, map, units, unit_count, fallback_sprite, cache,
                              game_info, mouse.x, mouse.y, -1);
    bool attack = target >= 0 && units[target]->owner != consoleplayer && units[target]->hp > 0;
    G_SelectedTiccmd(attack ? TC_ATTACK : TC_ORDER, units, unit_count, goal,
                     attack ? units[target]->id : 0);
}

void G_Responder(app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
                  const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                  const gameinfo_t *game_info, const SDL_Event *e) {
    switch (e->type) {
        case SDL_QUIT:
            app->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                R_RefreshViewport(app);
            }
            break;
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_ESCAPE) app->running = false;
            if (e->key.keysym.sym == SDLK_g) app->show_grid = !app->show_grid;
            if (e->key.keysym.sym == SDLK_b) app->show_blocked = !app->show_blocked;
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) {
                for (int i = 0; i < unit_count; ++i) {
                    P_MobjSetSelected(units[i], P_VisibleToPlayer(units[i]) &&
                        units[i]->owner == consoleplayer && (units[i]->traits & MF_SELECTABLE) != 0 &&
                        units[i]->hp > 0);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            R_WindowToRenderPt(app, e->motion.x, e->motion.y, &app->mouse.x, &app->mouse.y);
            if (app->panning) {
                float dx = 0.0f, dy = 0.0f;
                R_WindowToRenderDelta(app, e->motion.xrel, e->motion.yrel, &dx, &dy);
                app->cam.x += dx;
                app->cam.y += dy;
            }
            if (app->dragging_select) {
                int mx = 0, my = 0;
                R_WindowToRenderPt(app, e->motion.x, e->motion.y, &mx, &my);
                app->selection_rect = irect_from_points(app->mouse_down, (ivec2_t){ mx, my });
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e->button.button == SDL_BUTTON_LEFT) {
                R_WindowToRenderPt(app, e->button.x, e->button.y,
                                   &app->mouse_down.x, &app->mouse_down.y);
                app->dragging_select = true;
                app->selection_rect = (irect_t){ app->mouse_down.x, app->mouse_down.y, 0, 0 };
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                if (game_info && game_info->right_click_orders) {
                    ivec2_t mouse;
                    R_WindowToRenderPt(app, e->button.x, e->button.y, &mouse.x, &mouse.y);
                    order_selected_at(app, map, units, unit_count, fallback_sprite, cache,
                                      game_info, mouse);
                } else {
                    app->dragging_select = false;
                    app->selection_rect = (irect_t){0};
                    for (int i = 0; i < unit_count; ++i)
                        P_MobjSetSelected(units[i], false);
                }
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && app->dragging_select) {
                int bx = 0, by = 0;
                R_WindowToRenderPt(app, e->button.x, e->button.y, &bx, &by);
                irect_t rect = irect_from_points(app->mouse_down, (ivec2_t){ bx, by });
                bool box = rect.w > 5 || rect.h > 5;
                bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;
                app->dragging_select = false;
                app->selection_rect = (irect_t){0};
                int picked = box ? -1 : R_PickUnit(app, map, units, unit_count,
                    fallback_sprite, cache, game_info, bx, by, consoleplayer);
                if (!box && !additive && picked < 0 &&
                    !(game_info && game_info->right_click_orders)) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (P_MobjIsSelected(units[i]) && units[i]->owner == consoleplayer && units[i]->hp > 0) {
                            order_selected_at(app, map, units, unit_count, fallback_sprite,
                                              cache, game_info, (ivec2_t){bx, by});
                            return;
                        }
                    }
                }
                if (!additive) {
                    for (int i = 0; i < unit_count; ++i)
                        P_MobjSetSelected(units[i], false);
                }
                if (box) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (!P_VisibleToPlayer(units[i])) continue;
                        if (units[i]->hp <= 0) continue;
                        if ((units[i]->traits & MF_SELECTABLE) == 0) continue;
                        if (units[i]->owner != consoleplayer) continue;
                        irect_t visible;
                        float sx = 0.0f, sy = 0.0f;
                        unit_screen_rect_for_view(app, map, units[i], fallback_sprite, cache,
                                                  game_info, app->ticks_ms, NULL, &visible,
                                                  &sx, &sy, NULL, NULL, NULL);
                        float radius = unit_pick_radius_px(app, units[i]);
                        if (irect_intersects(visible, rect) ||
                            circle_intersects_rect((fvec2_t){ sx, sy }, radius, rect)) {
                            P_MobjSetSelected(units[i], true);
                        }
                    }
                } else {
                    if (picked >= 0) {
                        P_MobjSetSelected(units[picked], true);
                    }
                }
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = false;
            }
            break;
        case SDL_MOUSEWHEEL:
            app->cam.y += (float)e->wheel.y * 48.0f;
            app->cam.x += (float)e->wheel.x * 48.0f;
            break;
        default:
            break;
    }
}

void G_CameraMove(app_t *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) app->cam.x += speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) app->cam.x -= speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) app->cam.y += speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) app->cam.y -= speed;
}

void R_ClampCamera(app_t *app, const level_t *map, int viewport_w, int viewport_h) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    if (viewport_w <= 0) viewport_w = app->win.w;
    if (viewport_h <= 0) viewport_h = app->win.h;

    float map_w = (float)map->width * (float)app_cell_w(app);
    float map_h = (float)map->height * (float)app_cell_h(app);
    if (map_w <= (float)viewport_w) {
        app->cam.x = ((float)viewport_w - map_w) * 0.5f;
    } else {
        float min_x = (float)viewport_w - map_w;
        if (app->cam.x < min_x) app->cam.x = min_x;
        if (app->cam.x > 0.0f) app->cam.x = 0.0f;
    }
    if (map_h <= (float)viewport_h) {
        app->cam.y = ((float)viewport_h - map_h) * 0.5f;
    } else {
        float min_y = (float)viewport_h - map_h;
        if (app->cam.y < min_y) app->cam.y = min_y;
        if (app->cam.y > 0.0f) app->cam.y = 0.0f;
    }
}

void R_FreeTileset(tileset_t *tileset) {
    if (tileset->texture) SDL_DestroyTexture(tileset->texture);
    free(tileset->indices);
    free(tileset->palette_cycle.tiles);
    free(tileset->tile_lookup);
    free(tileset->animations);
    memset(tileset, 0, sizeof(*tileset));
}

void R_FreeSprite(spritesheet_t *sprite) {
    if (!sprite) return;
    for (int i = 0; i < sprite->numlumps; ++i) {
        spritelump_t *lump = &sprite->lumps[i];
        if (lump->texture) SDL_DestroyTexture(lump->texture);
        free(lump->indices);
    }
    free_sprite_def(&sprite->spritedef);
    free(sprite->cells);
    free(sprite->lumps);
    free(sprite->palette_maps);
    memset(sprite, 0, sizeof(*sprite));
}

void HU_FreeFont(bitmapfont_t *font) {
    if (!font) return;
    R_FreeSprite(&font->sprite);
    memset(font, 0, sizeof(*font));
}

void R_FreeSpriteCache(spritecache_t *cache) {
    if (cache->ui) {
        R_FreeSpriteCache(cache->ui);
        free(cache->ui);
    }
    for (int i = 0; i < cache->count; ++i) {
        R_FreeSprite(&cache->entries[i].sprite);
    }
    free(cache->sprites);
    memset(cache, 0, sizeof(*cache));
}
