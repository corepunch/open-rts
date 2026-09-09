#include "dc_facing.h"
#include "w_spr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "info.h"

static const char *companion_path(const char *path, const char *from, const char *extension,
                                  const char *to, const char *new_extension) {
    const char *base = M_FileName(path), *dot = strrchr(base, '.');
    size_t directory_length = strlen(from) + 1;
    if (!dot || strcasecmp(dot, extension) || (size_t)(base - path) < directory_length)
        return NULL;
    const char *directory = base - directory_length;
    if ((directory > path && directory[-1] != '/') ||
        strncasecmp(directory, from, directory_length - 1)) return NULL;
    return M_va("%.*s%s/%.*s%s", (int)(directory - path), path, to,
                (int)(dot - base), base, new_extension);
}

static const char *dependency_name(const char *dependency) {
    char *stem = M_Upper(M_va("%.8s", dependency));
    stem[strcspn(stem, " \t\r\n\v\f")] = '\0';
    if (!*stem || strspn(stem, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(stem))
        return NULL;
    return M_va("SPRITES/%s.SPR", stem);
}

static const char *sprite_stem(const char *path) {
    char *stem = M_Upper(M_va("%.8s", M_FileName(path)));
    stem[strcspn(stem, ".")] = '\0';
    return stem;
}

static bool asset_exists(const char *path) { return path && access(path, R_OK) == 0; }

static void decode_palette(const uint8_t palette[256][3], uint32_t colors[256]) {
    for (int i = 0; i < 256; ++i) {
        int r = clamp255((int)palette[i][0] * 4 + 3);
        int g = clamp255((int)palette[i][1] * 4 + 3);
        int b = clamp255((int)palette[i][2] * 4 + 3);
        colors[i] = i == 0 ? 0x00000000u :
            (0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
}

typedef struct {
    bool valid;
    uint32_t palette[256];
    uint8_t selector5[256 * 256];
} RenderTables;

static RenderTables render_tables;

static bool install_fin_parts(spriteframe_t *spriteframe, int rotation,
                              const dc_fin_t *animation, int frame_index,
                              const char *stem, int numlumps) {
    spritedirection_t decoded = {0};
    if (!DC_FINFrame(animation, frame_index, &decoded)) return false;
    bool body = false;
    for (spritelayer_t *part = decoded.layers; part->sprite_name[0]; ++part) {
        if (strcasecmp(part->sprite_name, stem) == 0) {
            if (!body && part->layer == 1) {
                if (part->lump >= numlumps) goto fail;
                body = true;
            }
            memset(part->sprite_name, 0, sizeof(part->sprite_name));
            part->sprite_name[0] = '.';
        } else {
            M_Upper(part->sprite_name);
        }
    }
    if (!body) goto fail;
    free(spriteframe->directions[rotation].layers);
    spriteframe->directions[rotation] = decoded;
    return true;
fail:
    free(decoded.layers);
    return false;
}

enum { FIN_ACTIONS = 8, FIN_DIRECTIONS = 16 };
static const char *const fin_actions[FIN_ACTIONS] = {
    "STAND", "MOVE", "FIREA", "FIREB", "FIRE", "DIEA", "DIEB", "DIEC",
};

typedef struct {
    const dc_fin_label_t *directions[FIN_DIRECTIONS];
} fin_sequence_t;

static int label_length(const dc_fin_label_t *label) {
    return label ? (int)SDL_SwapLE16(label->end) - SDL_SwapLE16(label->start) + 1 : 0;
}

/* Resolve names once. FIN ranges already contain the temporal frame ordering. */
static void collect_sequences(const dc_fin_t *fin, const char *stem,
                              fin_sequence_t sequences[FIN_ACTIONS]) {
    size_t prefix_length = strlen(stem);
    for (int i = 0; i < SDL_SwapLE16(fin->header->label_count); ++i) {
        const dc_fin_label_t *label = &fin->labels[i];
        if (strncmp(label->name, stem, prefix_length)) continue;
        char *action = M_va("%.*s", (int)(sizeof(label->name) - prefix_length),
                            label->name + prefix_length);
        char *number = action + strcspn(action, "0123456789"), *end;
        if (!*number) continue;
        long direction = strtol(number, &end, 10);
        if (*end || direction >= FIN_DIRECTIONS) continue;
        *number = '\0';
        for (int a = 0; a < FIN_ACTIONS; ++a) {
            /* EXPL's odd idle facings are authored under SHUF. */
            const char *expected = a == 0 && !strcmp(stem, "EXPL") && (direction & 1)
                ? "SHUF" : fin_actions[a];
            if (!strcmp(action, expected) && !sequences[a].directions[direction])
                sequences[a].directions[direction] = label;
        }
    }
}

static int sequence_rotations(const fin_sequence_t *sequence) {
    for (int stride = 1; stride <= 2; ++stride) {
        bool complete = true;
        for (int i = 0; i < FIN_DIRECTIONS; i += stride)
            if (label_length(sequence->directions[i]) <= 0) complete = false;
        if (complete) return FIN_DIRECTIONS / stride;
    }
    return 0;
}

static void install_fin_frames(spritesheet_t *sheet, const dc_fin_t *fin, const char *stem) {
    if (!fin->header) return;
    fin_sequence_t sequences[FIN_ACTIONS] = {0};
    collect_sequences(fin, stem, sequences);
    for (int action = 0; action < FIN_ACTIONS; ++action) {
        const fin_sequence_t *sequence = &sequences[action];
        int rotations = sequence_rotations(sequence);
        if (!rotations) continue;
        int stride = FIN_DIRECTIONS / rotations;
        int length = 0;
        for (int r = 0; r < rotations; ++r) {
            int count = label_length(sequence->directions[r * stride]);
            if (count > length) length = count;
        }
        const dc_fin_label_t *base = sequence->directions[0];
        sheet->spritedef.rotations = rotations;
        sheet->spritedef.first_angle = dc_fin_direction_to_angle(0);
        sheet->spritedef.clockwise = true;
        for (int frame = 0; frame < length; ++frame) {
            int index = SDL_SwapLE16(base->start) + frame;
            if (index >= sheet->spritedef.numframes) break;
            spriteframe_t *target = &sheet->spritedef.spriteframes[index];
            snprintf(target->frame_name, sizeof(target->frame_name), "%.16s", base->name);
            for (int r = 0; r < rotations; ++r) {
                const dc_fin_label_t *label = sequence->directions[r * stride];
                int count = label_length(label);
                int source = SDL_SwapLE16(label->start) + (frame < count ? frame : count - 1);
                install_fin_parts(target, r, fin, source, stem, sheet->numlumps);
            }
        }
    }
}

bool load_render_tables(const char *data_root, const char *tileset_name) {
    memset(&render_tables, 0, sizeof(render_tables));
    if (!data_root || !tileset_name || tileset_name[0] == '\0') return false;

    blob_t bts;
    if (!W_ReadFile(M_va("%s/SCENARIO/%s.BTS", data_root, tileset_name), &bts)) return false;
    if (bts.size < sizeof(dc_spr_header_t)) {
        W_FreeFile(&bts);
        return false;
    }
    const dc_spr_header_t *header = (const void *)bts.bytes;
    decode_palette(header->palette, render_tables.palette);
    W_FreeFile(&bts);

    blob_t rmp;
    if (!W_ReadFile(M_va("%s/%s.RMP", data_root, tileset_name), &rmp)) return false;
    if (rmp.size < 3 * 256 * 256) {
        W_FreeFile(&rmp);
        return false;
    }
    memcpy(render_tables.selector5, rmp.bytes + 256 * 256,
           sizeof(render_tables.selector5));
    W_FreeFile(&rmp);
    render_tables.valid = true;
    return true;
}

/* SPR is an 8-byte header, 256 RGB entries, 8-byte cell descriptors,
 * then consecutive raw pixels or length-prefixed skip/literal streams.
 * Read those spans directly from the file into the final indexed lumps. */
static bool decode_cell(uint8_t *dst, size_t pixels,
                        const uint8_t *src, size_t size, bool compressed) {
    if (!compressed) {
        memcpy(dst, src, size);
        return true;
    }
    size_t read = 0, write = 0;
    while (read < size && write < pixels) {
        int command = (int8_t)src[read++];
        size_t count = command < 0 ? (size_t)-command : (size_t)command + 1;
        if (count > pixels - write) return false;
        if (command >= 0) {
            if (count > size - read) return false;
            memcpy(dst + write, src + read, count);
            read += count;
        }
        write += count;
    }
    return true;
}

static irect_t cell_bounds(const uint8_t *indices, irect_t rect) {
    int min_x = rect.w, min_y = rect.h, max_x = -1, max_y = -1;
    for (int y = 0; y < rect.h; ++y) {
        for (int x = 0; x < rect.w; ++x) {
            if (!indices[y * rect.w + x]) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    return max_x < min_x ? rect :
        (irect_t){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

/* FIN supplies the anchor. Visit its command stream once, not once per cell. */
static bool install_ground_points(spritesheet_t *sheet, const dc_fin_t *fin, const char *stem) {
    if (!fin->command_count) return true;
    bool *installed = calloc((size_t)sheet->numlumps, sizeof(*installed));
    if (!installed) return false;
    for (int i = 0; i < fin->command_count; ++i) {
        spritelayer_t part;
        if (!DC_FINLayer(fin, i, &part) || part.layer != 1 ||
            part.lump >= sheet->numlumps || installed[part.lump] ||
            strcasecmp(part.sprite_name, stem) != 0) continue;
        spritecell_t *cell = &sheet->cells[part.lump];
        cell->ground_point = (ivec2_t){
            (part.flags & RTS_FRAME_FLIP_X) ? cell->rect.w + part.offset.x :
                                            -part.offset.x - cell->displacement.x,
            cell->rect.h - part.offset.y,
        };
        installed[part.lump] = true;
    }
    free(installed);
    return true;
}

static bool create_cell_textures(spritelump_t *lump, irect_t rect,
                                  const uint32_t palette[256]) {
    size_t count = (size_t)rect.w * (size_t)rect.h;
    uint32_t *rgba = malloc(count * sizeof(*rgba));
    if (!rgba) return false;
    bool team_colors = false;
    for (size_t i = 0; i < count; ++i) {
        uint8_t index = lump->indices[i];
        rgba[i] = palette[index];
        team_colors |= index >= 138 && index <= 143;
    }
    lump->texture = I_CreateTexture(r_renderer, rgba, rect.w, rect.h, true);
    if (!lump->texture) goto fail;
    if (team_colors) {
        lump->translations = calloc(8, sizeof(*lump->translations));
        if (!lump->translations) goto fail;
        for (int remap = 0; remap < 8; ++remap) {
            for (size_t i = 0; i < count; ++i) {
                int index = lump->indices[i];
                if (index >= 138 && index <= 143) index += (remap - 7) * 6;
                rgba[i] = palette[index];
            }
            SDL_Texture *texture = I_CreateTexture(
                r_renderer, rgba, rect.w, rect.h, true);
            if (!texture) goto fail;
            lump->translations[lump->translation_count++] =
                (spritetranslation_t){ .id = remap, .texture = texture };
        }
    }
    free(rgba);
    return true;
fail:
    free(rgba);
    return false; /* The sheet owns every texture already created. */
}

typedef struct {
    blob_t file;
    const dc_spr_header_t *header;
    const dc_spr_cell_t *cells;
    blob_t pixels;
    size_t payload_left;
    bool compressed;
} dc_spr_t;

static bool open_spr(const char *path, dc_spr_t *spr) {
    if (!W_ReadFile(path, &spr->file)) return false;
    spr->pixels = spr->file;
    spr->header = DC_TakeRecords(&spr->pixels, 1, sizeof(*spr->header));
    if (!spr->header || !SDL_SwapLE16(spr->header->cell_count)) return false;
    spr->cells = DC_TakeRecords(&spr->pixels, SDL_SwapLE16(spr->header->cell_count),
                               sizeof(*spr->cells));
    spr->compressed = (SDL_SwapLE16(spr->header->flags) & 0x180) != 0;
    spr->payload_left = SDL_SwapLE32(spr->header->payload_size);
    return spr->cells != NULL;
}

static bool load_cell(dc_spr_t *spr, int index,
                      spritesheet_t *sheet, const uint32_t palette[256]) {
    const dc_spr_cell_t *record = &spr->cells[index];
    isize2_t size = { SDL_SwapLE16(record->size.w), SDL_SwapLE16(record->size.h) };
    size_t bytes = (size_t)size.w * (size_t)size.h;
    if (spr->compressed) {
        /* Chunk lengths need byte reads: the preceding RLE span can be odd. */
        const uint8_t *length = DC_TakeRecords(&spr->pixels, 1, sizeof(uint32_t));
        if (!length) return false;
        bytes = read_u32_le(length);
        if (bytes > spr->payload_left) return false;
        spr->payload_left -= bytes;
    }
    const uint8_t *source = DC_TakeRecords(&spr->pixels, bytes, 1);
    if (!source) return false;
    spritecell_t *cell = &sheet->cells[index];
    spritelump_t *lump = &sheet->lumps[index];
    /* SDL cannot create a zero-sized texture; empty native cells stay transparent. */
    cell->rect = (irect_t){ 0, 0, size.w ? size.w : 1, size.h ? size.h : 1 };
    cell->displacement = (ivec2_t){ SDL_SwapLE16(record->displacement.x),
                                   SDL_SwapLE16(record->displacement.y) };
    size_t pixels = (size_t)cell->rect.w * (size_t)cell->rect.h;
    lump->indices = calloc(pixels, 1);
    if (!lump->indices || (size.w && size.h &&
        !decode_cell(lump->indices, pixels, source, bytes, spr->compressed))) return false;
    cell->bounds = cell_bounds(lump->indices, cell->rect);
    cell->ground_point = (ivec2_t){ cell->bounds.x + cell->bounds.w / 2,
                                   cell->bounds.y + cell->bounds.h };
    return create_cell_textures(lump, cell->rect, palette);
}

static bool load_cells(dc_spr_t *spr, spritesheet_t *out, const uint32_t palette[256]) {
    int count = SDL_SwapLE16(spr->header->cell_count);
    if (!R_AllocSpriteCells(out, count)) return false;
    out->frame_size = (isize2_t){ 1, 1 };
    for (int i = 0; i < count; ++i) {
        if (!load_cell(spr, i, out, palette)) return false;
        const spritecell_t *cell = &out->cells[i];
        isize2_t extent = { cell->displacement.x + cell->rect.w,
                           cell->displacement.y + cell->rect.h };
        out->frame_size = isize2_max(out->frame_size, extent);
    }
    return true;
}

static bool init_sprite_frames(spritesheet_t *sheet, const dc_fin_t *fin) {
    int count = fin->header ? SDL_SwapLE16(fin->header->frame_count) : 0;
    if (count < sheet->numlumps) count = sheet->numlumps;
    if (!R_InitSpriteDef(sheet, count, 1, ANG270, false)) return false;
    for (int i = 0; i < sheet->numlumps; ++i) {
        spritelayer_t *layer = calloc(2, sizeof(*layer));
        if (!layer) return false;
        *layer = (spritelayer_t){ .sprite_name = ".", .lump = i, .intensity = 16 };
        sheet->spritedef.spriteframes[i].directions[0].layers = layer;
    }
    return true;
}

static bool load_sprite(const char *path, spritesheet_t *out,
                         uint32_t palette_out[256], dc_fin_t *animation_out) {
    memset(out, 0, sizeof(*out));
    dc_spr_t spr = {0};
    dc_fin_t fin = {0};
    uint32_t palette[256];
    const char *sprite_path = path;
    const char *extension = strrchr(M_FileName(path), '.');
    if (extension && strcasecmp(extension, ".FIN") == 0) {
        if (!DC_LoadFIN(path, &fin)) goto fail;
        sprite_path = companion_path(path, "ANIMATE", ".FIN", "SPRITES", ".SPR");
    } else {
        const char *animation = companion_path(path, "SPRITES", ".SPR", "ANIMATE", ".FIN");
        if (asset_exists(animation)) DC_LoadFIN(animation, &fin);
    }
    if (!sprite_path || !open_spr(sprite_path, &spr)) goto fail;
    decode_palette(spr.header->palette, palette);
    if (palette_out) memcpy(palette_out, palette, sizeof(palette));
    char stem[9]; /* Retained while later M_va calls reuse their temporary strings. */
    snprintf(stem, sizeof(stem), "%s", sprite_stem(sprite_path));
    if (!load_cells(&spr, out, palette) || !init_sprite_frames(out, &fin) ||
        !install_ground_points(out, &fin, stem)) goto fail;
    install_fin_frames(out, &fin, stem);
    out->indexed = true;
    memcpy(out->palette, render_tables.valid ? render_tables.palette : palette,
           sizeof(out->palette));
    if (render_tables.valid) {
        out->indexed_blend_selector = 5;
        out->indexed_blend_table = render_tables.selector5;
    }
    if (animation_out) {
        *animation_out = fin;
        memset(&fin, 0, sizeof(fin));
    }
    W_FreeFile(&spr.file);
    DC_FreeFIN(&fin);
    return true;
fail:
    W_FreeFile(&spr.file);
    DC_FreeFIN(&fin);
    R_FreeSprite(out);
    return false;
}

bool load_dark_colony_sprite(const char *path, spritesheet_t *out,
                             uint32_t palette_out[256]) {
    return load_sprite(path, out, palette_out, NULL);
}

static const char *resolve_sprite_path(const char *root, const char *name) {
    if (*name == '/') return name;
    if (strchr(name, '/')) return M_va("%s/%s", root, name);
    static const char *const locations[] = {
        "ANIMATE", "SPRITES", "CURSOR", "ENCYCLO", "INTRFACE",
    };
    for (size_t i = 0; i < sizeof(locations) / sizeof(*locations); ++i) {
        const char *path = M_va("%s/%s/%s.%s", root, locations[i], name, i ? "SPR" : "FIN");
        if (asset_exists(path)) return path;
    }
    return NULL;
}

static bool sprite_cache_load_dark_colony(spritecache_t *cache,
                                          const char *data_root, const char *name) {
    if (!name || name[0] == '\0') return true;
    if (R_CacheFind(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many Dark Colony sprites; skipped %s\n", name);
        return false;
    }
    cachedsprite_t *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    const char *sprite_path = resolve_sprite_path(data_root, name);
    if (!sprite_path) {
        fprintf(stderr, "failed to resolve Dark Colony sprite %s\n", entry->name);
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    dc_fin_t animation = {0};
    if (!load_sprite(sprite_path, &entry->sprite, NULL, &animation)) {
        fprintf(stderr, "failed to load %s\n", entry->name);
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    cache->count++;
    if (DC_FINCommandCount(&animation) > 0) {
        for (int i = 0; i < SDL_SwapLE16(animation.header->dependency_count); ++i) {
            const char *dependency = dependency_name(animation.dependencies[i].name);
            if (!dependency || R_CacheFind(cache, dependency) ||
                !asset_exists(M_va("%s/%s", data_root, dependency))) continue;
            if (!sprite_cache_load_dark_colony(cache, data_root, dependency)) {
                DC_FreeFIN(&animation);
                return false;
            }
        }
    }
    DC_FreeFIN(&animation);
    return true;
}

bool load_dark_colony_unit_sprites(const char *data_root,
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache) {
    bool ok = true;
    static const char *const ui_sprites[] = {
        "INTRFACE/DCSS.SPR",
        "INTRFACE/DCUT.SPR",
        "INTRFACE/MAINBUT.SPR",
        "INTRFACE/SHUMANE.SPR",
        "SPRITES/DROP.SPR",
        "SPRITES/BEAC.SPR",
        "SPRITES/MUZA.SPR",
        "SPRITES/BLOO.SPR",
    };
    for (int i = 0; i < NUMSTATES; ++i) {
        int sprite = states[i].sprite;
        if (sprite >= 0 && sprite < NUMSPRITES)
            ok &= sprite_cache_load_dark_colony(cache, data_root, sprnames[sprite]);
    }
    for (size_t i = 0; i < sizeof(ui_sprites) / sizeof(*ui_sprites); ++i)
        ok &= sprite_cache_load_dark_colony(cache, data_root, ui_sprites[i]);
    int selection_sprite = game_info.selection_marker.sprite;
    if (selection_sprite >= 0 && selection_sprite < NUMSPRITES)
        ok &= sprite_cache_load_dark_colony(cache, data_root, sprnames[selection_sprite]);
    if (map) {
        for (int i = 0; i < map->decoration_count; ++i) {
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].sprite_name);
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].sprite2_name);
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].shadow_name);
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobjtype_t *info = units[i].info;
        ok &= sprite_cache_load_dark_colony(cache, data_root, units[i].core.sprite_name);
        ok &= sprite_cache_load_dark_colony(cache, data_root, info ? info->shadow_name : NULL);
        ok &= sprite_cache_load_dark_colony(cache, data_root, info ? info->hit_effect_name : NULL);
    }
    return ok;
}
