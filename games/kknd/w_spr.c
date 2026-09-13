#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"
#include "game.h"
#include "info.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAX_ANIMATIONS = 512, MAX_FRAMES = 4096 };

typedef struct {
    uint32_t animations[MAX_ANIMATIONS];
    int count;
    uint32_t table, first_frame;
} mobd_layout_t;

static bool mobd_layout(const uint8_t *segment, size_t size, uint32_t member,
                        mobd_layout_t *out) {
    *out = (mobd_layout_t){.first_frame = (uint32_t)size};
    uint32_t pos = member;
    while (pos < out->first_frame && out->count < MAX_ANIMATIONS) {
        if (!range_ok(size, pos, 4)) return false;
        int32_t value = read_i32_le(segment + pos);
        pos += 4;
        if (value == 0 || (value < (int32_t)pos && value >= (int32_t)member)) {
            pos -= 4;
            break;
        }
        out->animations[out->count++] = pos - 4;
        while (true) {
            if (!range_ok(size, pos, 4)) return false;
            value = read_i32_le(segment + pos);
            pos += 4;
            if (value == 0 || value == -1) break;
            if (value < 0 || (uint32_t)value >= size) return false;
            if ((uint32_t)value < out->first_frame) out->first_frame = (uint32_t)value;
        }
    }
    out->table = pos;
    return pos <= out->first_frame && (out->first_frame - pos) % (16 * 4) == 0;
}
static bool build_map_tileset(SDL_Renderer *renderer, const KkndMapData *map,
                                   tileset_t *out) {
    out->texture = I_CreateTexture(renderer, map->pixels, map->atlas.w, map->atlas.h, true);
    if (!out->texture) return false;
    out->count = map->tile_count;
    out->atlas_cols = 64;
    out->tile_w = 32;
    out->tile_h = 32;
    return true;
}

static int sprite_member_index(const char *name) {
    if (!name || !*name) return -1;
    if (isdigit((unsigned char)name[0])) return atoi(name);
    static const struct { const char *name; int index; } names[] = {
        { "Infantry.mobd", 34 }, { "Shotgunner.mobd", 68 },
        { "Bike.mobd", 7 }, { "Cursors.mobd", 17 },
        { "Tank.mobd", 77 }, { "Technician.mobd", 78 },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcasecmp(name, names[i].name) == 0) return names[i].index;
    return -1;
}

static bool decode_mobd_image(const uint8_t *segment, size_t size,
                               uint32_t frame_offset,
                               spritecell_t *cell, spritelump_t *lump, bool *flip_out) {
    if (!range_ok(size, frame_offset, 28)) return false;
    ivec2_t offset = { read_i32_le(segment + frame_offset), read_i32_le(segment + frame_offset + 4) };
    uint32_t flags_offset = read_u32_le(segment + frame_offset + 12);
    if (!range_ok(size, flags_offset, 12) || memcmp(segment + flags_offset, "TRPS", 4) != 0)
        return false;
    uint32_t flags = read_u32_le(segment + flags_offset + 4);
    *flip_out = (flags & 1u) != 0;
    uint32_t image = read_u32_le(segment + flags_offset + 8);
    if (!range_ok(size, image, 9)) return false;
    int width = (int)read_u32_le(segment + image);
    int height = (int)read_u32_le(segment + image + 4);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return false;
    size_t count = (size_t)width * (size_t)height;
    uint8_t *indices = calloc(count, 1); /* index 0 = transparent */
    if (!indices) return false;
    uint32_t pos = image + 9;
    if (segment[image + 8] == 2) {
        size_t write = 0;
        while (write < count) {
            if (!range_ok(size, pos, 1)) goto fail;
            uint32_t line_bytes = (uint32_t)segment[pos++];
            if (line_bytes == 0) goto fail;
            line_bytes--;
            if (!range_ok(size, pos, line_bytes)) goto fail;
            uint32_t end = pos + line_bytes;
            bool skip = true;
            while (pos < end) {
                uint8_t chunk = segment[pos++];
                if ((size_t)chunk > count - write) goto fail;
                if (skip) write += chunk; /* gap pixels stay 0 (transparent) */
                else {
                    if (chunk > end - pos) goto fail;
                    memcpy(indices + write, segment + pos, chunk);
                    write += chunk;
                    pos += chunk;
                }
                skip = !skip;
            }
            if (write < count) write += ((size_t)width - write % (size_t)width) % (size_t)width;
        }
    } else {
        if (!range_ok(size, pos, count)) goto fail;
        memcpy(indices, segment + pos, count);
    }
    cell->rect = cell->bounds = (irect_t){ 0, 0, width, height };
    /* MOBD offsets anchor the final, mirrored image. The renderer mirrors
     * ground_point with the pixels, so store it in unmirrored coordinates. */
    cell->ground_point = *flip_out ? (ivec2_t){ width - offset.x, offset.y } : offset;
    lump->indices = indices;
    return true;
fail:
    free(indices);
    return false;
}

static bool weapon_point(const uint8_t *segment, size_t size, uint32_t frame, ivec2_t *out) {
    *out = (ivec2_t){0, 0};
    if (!range_ok(size, frame, 28)) return false;
    uint32_t point = read_u32_le(segment + frame + 24);
    if (!point) return true; /* Optional MOBD point list: use the actor origin. */
    while (range_ok(size, point, 4)) {
        int32_t id = read_i32_le(segment + point);
        if (id == -1) return true;
        if (!range_ok(size, point, 16)) return false;
        if (id == 0) {
            *out = (ivec2_t){ read_i32_le(segment + point + 4) >> 8,
                             read_i32_le(segment + point + 8) >> 8 };
            return true;
        }
        point += 16;
    }
    return false;
}

static ivec2_t layer_anchor(const spritesheet_t *sprite, const spritelayer_t *layer) {
    const spritecell_t *cell = &sprite->cells[layer->lump];
    return layer->flags & RTS_FRAME_FLIP_X ?
        (ivec2_t){cell->rect.w - cell->ground_point.x, cell->ground_point.y} : cell->ground_point;
}

static bool compose_shooting(const uint8_t *segment, size_t size, const uint32_t *frames,
                             spritesheet_t *out, int native_frames,
                             const mobjinfo_t *info, const spritesheet_t *extras,
                             const spritesheet_t *turret) {
    if (states[info->missilestate].frame != native_frames ||
        info->muzzle.body < 0 || info->muzzle.body >= native_frames ||
        info->muzzle.frame < 0 || info->muzzle.frame + 2 > extras->spritedef.numframes)
        return false;
    /* A turret's point 0 is relative to the body's point 0. Keep native poses
     * and image anchors; no sprite-name aliases or placement adjustments. */
    ivec2_t turret_points[16] = {0};
    if (info->muzzle.turret) {
        uint32_t member;
        mobd_layout_t layout;
        if (!turret || !lvl_asset(segment, size, "MOBD",
                sprite_member_index(sprnames[info->muzzle.turret]), &member) ||
            !mobd_layout(segment, size, member, &layout)) return false;
        /* The first populated channel is the turret's first logical frame. */
        uint32_t channel = layout.table;
        while (channel < layout.first_frame && !read_u32_le(segment + channel)) channel += 64;
        if (!range_ok(size, channel, 64) || channel + 64 > layout.first_frame) return false;
        for (int rotation = 0; rotation < 16; ++rotation) {
            uint32_t animation = read_u32_le(segment + channel + ((16 - rotation) % 16) * 4);
            if (!animation || !range_ok(size, animation, 8) ||
                !weapon_point(segment, size, read_u32_le(segment + animation + 4), &turret_points[rotation]))
                return false;
        }
        for (int frame = info->muzzle.body; frame < native_frames; ++frame) {
            for (int rotation = 0; rotation < 16; ++rotation) {
                spritedirection_t *direction = &out->spritedef.spriteframes[frame].directions[rotation];
                const spritelayer_t *turret_layer = turret->spritedef.spriteframes[0].directions[rotation].layers;
                ivec2_t point;
                if (!weapon_point(segment, size, frames[direction->layers[0].lump], &point)) return false;
                spritelayer_t *layers = calloc(3, sizeof(*layers));
                if (!layers) return false;
                layers[0] = direction->layers[0];
                layers[1] = *turret_layer;
                snprintf(layers[1].sprite_name, sizeof(layers[1].sprite_name),
                         "%s", sprnames[info->muzzle.turret]);
                layers[1].offset = ivec2_add(layer_anchor(out, layers),
                    ivec2_sub(point, layer_anchor(turret, turret_layer)));
                free(direction->layers);
                direction->layers = layers;
            }
        }
    }
    for (int rotation = 0; rotation < 16; ++rotation) {
        const spritelayer_t *body = out->spritedef.spriteframes[info->muzzle.body].directions[rotation].layers;
        ivec2_t point;
        if (!weapon_point(segment, size, frames[body->lump], &point)) return false;
        int body_layers = info->muzzle.turret ? 2 : 1;
        if (info->muzzle.turret) point = ivec2_add(point, turret_points[rotation]);
        for (int frame = 0; frame < 2; ++frame) {
            const spritelayer_t *flash = extras->spritedef.spriteframes[info->muzzle.frame + frame].directions[rotation].layers;
            spritelayer_t *layers = calloc((size_t)body_layers + 2, sizeof(*layers));
            if (!layers) return false;
            memcpy(layers, body, (size_t)body_layers * sizeof(*layers));
            layers[body_layers] = *flash;
            snprintf(layers[body_layers].sprite_name, sizeof(layers[body_layers].sprite_name),
                     "%s", sprnames[info->muzzle.sprite]);
            layers[body_layers].offset = ivec2_add(layer_anchor(out, body),
                ivec2_sub(point, layer_anchor(extras, flash)));
            out->spritedef.spriteframes[native_frames + frame].directions[rotation].layers = layers;
        }
    }
    return true;
}

static bool decode_mobd(const uint8_t *segment, size_t size,
                       uint32_t member, const uint32_t palette[256], spritesheet_t *out,
                       const mobjinfo_t *info, const spritesheet_t *extras,
                       const spritesheet_t *turret) {
    mobd_layout_t layout;
    if (!mobd_layout(segment, size, member, &layout)) return false;
    uint32_t *animation_offsets = layout.animations;
    int animation_count = layout.count;
    uint32_t first_frame = layout.first_frame;
    uint32_t pos = layout.table;

    uint32_t ordered[MAX_ANIMATIONS];
    int channels[MAX_ANIMATIONS], directions[MAX_ANIMATIONS];
    int channel_lengths[MAX_ANIMATIONS] = {0}, channel_groups[MAX_ANIMATIONS] = {0};
    int ordered_count = 0;
    uint32_t table_start = pos;
    if ((first_frame - table_start) % (16 * 4) != 0) goto fail;
    int channel_count = (int)((first_frame - table_start) / (16 * 4));
    if (channel_count > MAX_ANIMATIONS) goto fail;
    while (pos < first_frame && ordered_count < MAX_ANIMATIONS) {
        if (!range_ok(size, pos, 4)) return false;
        uint32_t value = read_u32_le(segment + pos);
        pos += 4;
        if (value == 0) continue;
        channels[ordered_count] = (int)((pos - table_start - 4) / (16 * 4));
        directions[ordered_count] = (int)((pos - table_start - 4) / 4 % 16);
        ordered[ordered_count++] = value;
        for (int i = 0; i < animation_count; ++i)
            if (animation_offsets[i] == value) animation_offsets[i] = 0;
    }
    for (int i = 0; i < animation_count; ++i) {
        if (!animation_offsets[i]) continue;
        if (ordered_count >= MAX_ANIMATIONS || channel_count >= MAX_ANIMATIONS) goto fail;
        channels[ordered_count] = channel_count++;
        directions[ordered_count] = 0;
        ordered[ordered_count++] = animation_offsets[i];
    }

    uint32_t frames[MAX_FRAMES];
    int group_starts[MAX_ANIMATIONS] = {0};
    int group_lengths[MAX_ANIMATIONS] = {0};
    int frame_count = 0;
    for (int group = 0; group < ordered_count; ++group) {
        uint32_t cursor = ordered[group];
        if (!range_ok(size, cursor, 8)) goto fail;
        cursor += 4; /* timing word */
        group_starts[group] = frame_count;
        while (frame_count < MAX_FRAMES) {
            if (!range_ok(size, cursor, 4)) goto fail;
            int32_t frame = read_i32_le(segment + cursor);
            cursor += 4;
            if (frame == 0 || frame == -1) break;
            if (frame < 0) goto fail;
            frames[frame_count++] = (uint32_t)frame;
            group_lengths[group]++;
        }
        int channel = channels[group];
        if (group_lengths[group] > channel_lengths[channel])
            channel_lengths[channel] = group_lengths[group];
        if (group_lengths[group]) channel_groups[channel]++;
    }
    if (frame_count == 0) goto fail;

    bool flips[MAX_FRAMES] = {0};
    if (!R_AllocSpriteCells(out, frame_count)) goto fail;
    out->frame_size = (isize2_t){ 1, 1 };
    for (int i = 0; i < frame_count; ++i) {
        spritecell_t *cell = &out->cells[i];
        if (!decode_mobd_image(segment, size, frames[i], cell, &out->lumps[i], &flips[i])) goto fail;
        if (cell->rect.w > out->frame_size.w) out->frame_size.w = cell->rect.w;
        if (cell->rect.h > out->frame_size.h) out->frame_size.h = cell->rect.h;
    }

    memcpy(out->palette,        palette, sizeof(out->palette));
    memcpy(out->source_palette, palette, sizeof(out->source_palette));
    out->palette[0] = out->source_palette[0] = 0x00000000u; /* index 0 = transparent */
    out->indexed = true;

    /* Native animation tables keep sixteen slots per channel. Complete
       direction sets share logical frames; sparse channels retain their
       individual sequences (e.g. the two Dire Wolf death sequences). */
    int logical_frames = 0;
    for (int channel = 0; channel < channel_count; ++channel) {
        if (channel_groups[channel] == 16) logical_frames += channel_lengths[channel];
        else for (int group = 0; group < ordered_count; ++group)
            if (channels[group] == channel) logical_frames += group_lengths[group];
    }
    bool compose = info && info->muzzle.sprite && extras;
    if (logical_frames <= 0 || !R_InitSpriteDef(out, logical_frames + (compose ? 2 : 0), 16)) goto fail;
    int logical_frame = 0;
    for (int channel = 0; channel < channel_count; ++channel) {
        for (int group = 0; group < ordered_count; ++group) {
            if (channels[group] != channel) continue;
            for (int frame = 0; frame < group_lengths[group]; ++frame) {
                int lump = group_starts[group] + frame;
                if (channel_groups[channel] != 16) {
                    for (int rotation = 0; rotation < 16; ++rotation)
                        R_InstallSpriteLump(out, logical_frame + frame, rotation, lump, flips[lump]);
                } else {
                    R_InstallSpriteLump(out, logical_frame + frame,
                        (16 - directions[group]) % 16, lump, flips[lump]);
                }
            }
            if (channel_groups[channel] != 16) logical_frame += group_lengths[group];
        }
        if (channel_groups[channel] == 16) logical_frame += channel_lengths[channel];
    }
    if (compose && !compose_shooting(segment, size, frames, out, logical_frames, info, extras, turret)) goto fail;
    return true;

fail:
    R_FreeSprite(out);
    return false;
}

static bool load_sprite(const char *data_root,
                       const char *spec, const uint32_t palette[256], spritesheet_t *out,
                       const spritesheet_t *extras, const spritesheet_t *turret) {
    char archive_rel[768];
    const char *member_name = NULL;
    const char *bar = spec ? strrchr(spec, '|') : NULL;
    if (bar) {
        size_t len = (size_t)(bar - spec);
        if (len >= sizeof(archive_rel)) return false;
        memcpy(archive_rel, spec, len);
        archive_rel[len] = '\0';
        member_name = bar + 1;
    } else {
        snprintf(archive_rel, sizeof(archive_rel), "LEVELS/640/SPRITES.LVL");
        member_name = spec;
    }
    int member_index = sprite_member_index(member_name);
    if (member_index < 0) {
        fprintf(stderr, "unknown KKnD MOBD member '%s' (use a numeric member such as 34.mobd)\n",
                member_name ? member_name : "");
        return false;
    }
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, archive_rel);
    blob_t blob = {0};
    const uint8_t *segment = NULL;
    size_t segment_size = 0;
    if (!open_lvl(path, &blob, &segment, &segment_size)) return false;
    uint32_t member = 0;
    const mobjinfo_t *info = NULL;
    if (extras) {
        for (int i = 1; i < NUMMOBJTYPES; ++i) {
            const mobjinfo_t *candidate = &mobjinfo[i];
            if (candidate->muzzle.sprite &&
                sprite_member_index(sprnames[states[candidate->spawnstate].sprite]) == member_index) {
                info = candidate;
                break;
            }
        }
    }
    bool ok = lvl_asset(segment, segment_size, "MOBD", member_index, &member) &&
              decode_mobd(segment, segment_size, member, palette, out, info, extras, turret);
    if (!ok) fprintf(stderr, "failed to decode MOBD member %d from %s\n", member_index, path);
    W_FreeFile(&blob);
    return ok;
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                   mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)renderer;
    const KkndMapData *native = map ? map->native_data : NULL;
    if (!native || !cache) return false;
    const char *extra_name = sprnames[SPR_EXTRAS];
    cachedsprite_t *cached_extras = R_CacheFind(cache, extra_name);
    const spritesheet_t *extras = cached_extras ? &cached_extras->sprite : NULL;
    if (!extras) {
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *entry = &cache->entries[cache->count];
        if (!load_sprite(root, extra_name, native->palette, &entry->sprite, NULL, NULL)) return false;
        snprintf(entry->name, sizeof(entry->name), "%s", extra_name);
        cache->count++;
        extras = &entry->sprite;
    }
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        const char *name = mobjs[i]->core.sprite_name;
        if (R_CacheFind(cache, name)) continue;
        const spritesheet_t *turret = NULL;
        int type = mobjs[i]->type_id;
        int turret_id = type > 0 && type < NUMMOBJTYPES ? mobjinfo[type].muzzle.turret : 0;
        if (turret_id) {
            const char *turret_name = sprnames[turret_id];
            cachedsprite_t *entry = R_CacheFind(cache, turret_name);
            if (!entry) {
                if (cache->count >= MAX_DECORATION_SPRITES) return false;
                entry = &cache->entries[cache->count];
                if (!load_sprite(root, turret_name, native->palette, &entry->sprite, NULL, NULL)) return false;
                snprintf(entry->name, sizeof(entry->name), "%s", turret_name);
                cache->count++;
            }
            turret = &entry->sprite;
        }
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *entry = &cache->entries[cache->count];
        if (!load_sprite(root, name, native->palette, &entry->sprite, extras, turret)) {
            ok = false;
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        cache->count++;
    }
    return R_BindSprites(cache, gameinfo) && ok;
}

bool load_assets(SDL_Renderer *renderer, const char *data_root,
                      const level_t *map, const char *sprite_name,
                      tileset_t *tileset, spritesheet_t *unit_sprite) {
    const KkndMapData *native = map ? map->native_data : NULL;
    if (!native || !build_map_tileset(renderer, native, tileset)) return false;
    if (!load_sprite(data_root, sprite_name, native->palette, unit_sprite, NULL, NULL)) {
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}

bool G_LoadMenuSprite(SDL_Renderer *renderer, const char *root,
                      const char *name, spritesheet_t *out) {
    (void)renderer; (void)root; (void)name; (void)out;
    return false;
}
