#include "w_spr.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "info.h"

static const char *dependency_name(const char *dependency) {
    char *stem = M_Upper(M_va("%.8s", dependency));
    stem[strcspn(stem, " \t\r\n\v\f")] = '\0';
    if (!*stem || strspn(stem, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(stem))
        return NULL;
    return M_va("SPRITES/%s.SPR", stem);
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

static bool install_fin_frame(spritedirection_t *direction, const dc_fin_t *fin,
                               int frame, const char *stem) {
    if (!DC_FINFrame(fin, frame, direction)) return false;
    for (spritelayer_t *part = direction->layers; part->sprite_name[0]; ++part) {
        M_Upper(part->sprite_name);
        if (!strcmp(part->sprite_name, stem)) {
            memset(part->sprite_name, 0, sizeof(part->sprite_name));
            part->sprite_name[0] = '.';
        }
    }
    return true;
}

/* Raw SPR cells come first, followed by every native FIN frame. */
static bool install_sprite_frames(spritesheet_t *sheet, const dc_fin_t *fin,
                                   const char *stem) {
    int count = fin->header ? SDL_SwapLE16(fin->header->frame_count) : 0;
    if (!R_InitSpriteDef(sheet, count + sheet->numlumps, 1)) return false;
    for (int i = 0; i < sheet->numlumps; ++i)
        if (!R_InstallSpriteLump(sheet, i, 0, i, false)) return false;
    for (int i = 0; i < count; ++i)
        if (!install_fin_frame(&sheet->spritedef.spriteframes[sheet->numlumps + i].directions[0], fin, i, stem))
            return false;
    if (!fin->header) return true;
    for (int i = 0; i < SDL_SwapLE16(fin->header->label_count); ++i) {
        const dc_fin_label_t *label = &fin->labels[i];
        for (int f = SDL_SwapLE16(label->start); f <= SDL_SwapLE16(label->end) && f < count; ++f) {
            spriteframe_t *frame = &sheet->spritedef.spriteframes[sheet->numlumps + f];
            snprintf(frame->frame_name, sizeof(frame->frame_name), "%.16s", label->name);
        }
    }
    /* Like Doom's lump suffixes, FIN label suffixes identify rotations. */
    for (int i = 0; i < SDL_SwapLE16(fin->header->label_count); ++i) {
        const dc_fin_label_t *base = &fin->labels[i];
        char name[17];
        snprintf(name, sizeof(name), "%.16s", base->name);
        int length = (int)strlen(name);
        if (length < 2 || name[length - 1] != '0' ||
            isdigit((unsigned char)name[length - 2])) continue;
        name[--length] = '\0';
        int start = SDL_SwapLE16(base->start), end = SDL_SwapLE16(base->end);
        if (start > end || end >= count) continue;
        const dc_fin_label_t *directions[16] = {0};
        for (int d = 0; d < 16; ++d) {
            const dc_fin_label_t *label = DC_FINLabel(fin, M_va("%s%d", name, d));
            if (label && SDL_SwapLE16(label->start) <= SDL_SwapLE16(label->end) &&
                SDL_SwapLE16(label->end) < count)
                directions[d] = label;
        }
        int stride;
        for (stride = 1; stride <= 2; ++stride) {
            int d;
            for (d = 0; d < 16 && directions[d]; d += stride) {}
            if (d == 16) break;
        }
        if (stride > 2) continue;
        int rotations = 16 / stride;
        for (int f = start; f <= end; ++f) {
            spriteframe_t *frame = &sheet->spritedef.spriteframes[sheet->numlumps + f];
            frame->rotations = rotations;
            for (int r = 0; r < rotations; ++r) {
                int d = ((rotations / 2 - r + rotations) % rotations) * stride;
                int source = SDL_SwapLE16(directions[d]->start) + f - start;
                if (source > SDL_SwapLE16(directions[d]->end))
                    source = SDL_SwapLE16(directions[d]->end);
                if (!install_fin_frame(&frame->directions[r], fin, source, stem)) return false;
            }
        }
    }
    return true;
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

/* FIN supplies the anchor. Visit its command stream once, not once per cell. */
static bool install_ground_points(spritesheet_t *sheet, const dc_fin_t *fin, const char *stem) {
    if (!fin->command_count || !sheet->numlumps) return true;
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
    cell->bounds = cell->rect;
    cell->ground_point = (ivec2_t){ cell->rect.w / 2, cell->rect.h };
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

static bool load_sprite(const char *path, spritesheet_t *out,
                         uint32_t palette_out[256], dc_fin_t *animation_out) {
    memset(out, 0, sizeof(*out));
    dc_spr_t spr = {0};
    dc_fin_t fin = {0};
    uint32_t palette[256] = {0};
    const char *sprite_path = path;
    char *other = M_va("%s", path);
    if (!other) goto fail;
    char *base = (char *)M_FileName(other), *extension = strrchr(base, '.');
    bool fin_file = extension && !strcasecmp(extension, ".FIN");
    char *folder = base - other >= 8 ? base - 8 : NULL;
    if (folder && (folder == other || folder[-1] == '/') && extension &&
        !strncasecmp(folder, fin_file ? "ANIMATE/" : "SPRITES/", 8) &&
        !strcasecmp(extension, fin_file ? ".FIN" : ".SPR")) {
        /* Directory names and extensions have equal lengths: swap in place. */
        memcpy(folder, fin_file ? "SPRITES/" : "ANIMATE/", 8);
        memcpy(extension, fin_file ? ".SPR" : ".FIN", sizeof(".SPR"));
    } else {
        other = NULL;
    }
    if (fin_file) {
        if (!other || !DC_LoadFIN(path, &fin)) goto fail;
        sprite_path = asset_exists(other) ? other : NULL;
    } else if (asset_exists(other)) {
        DC_LoadFIN(other, &fin);
    }
    if (sprite_path) {
        if (!open_spr(sprite_path, &spr)) goto fail;
        decode_palette(spr.header->palette, palette);
        if (!load_cells(&spr, out, palette)) goto fail;
    }
    if (palette_out) memcpy(palette_out, palette, sizeof(palette));
    char stem[9]; /* Retained while later M_va calls reuse their temporary strings. */
    snprintf(stem, sizeof(stem), "%.8s", M_FileName(path));
    stem[strcspn(stem, ".")] = '\0';
    M_Upper(stem);
    if (!install_sprite_frames(out, &fin, stem) ||
        !install_ground_points(out, &fin, stem)) goto fail;
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
        "ANIMATE", "SPRITES",
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
    if (animation.command_count > 0) {
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

static bool load_ui_sprites(const char *root, spritecache_t *cache) {
    static const char *const directories[] = { "CURSOR", "ENCYCLO", "INTRFACE" };
    bool ok = true;
    for (size_t i = 0; i < sizeof(directories) / sizeof(*directories); ++i) {
        DIR *dir = opendir(M_va("%s/%s", root, directories[i]));
        if (!dir) return false;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            const char *extension = strrchr(entry->d_name, '.');
            if (extension && !strcasecmp(extension, ".SPR"))
                ok &= sprite_cache_load_dark_colony(
                    cache, root, M_va("%s/%s", directories[i], entry->d_name));
        }
        closedir(dir);
    }
    return ok;
}

bool load_dark_colony_unit_sprites(const char *data_root,
                                   const level_t *map, mobj_t *const *units, int unit_count,
                                   spritecache_t *cache) {
    bool ok = true;
    static const char *const effect_sprites[] = {
        "SPRITES/DROP.SPR",
        "SPRITES/BEAC.SPR",
        "SPRITES/MUZA.SPR",
        "SPRITES/BLOO.SPR",
    };
    for (int i = 0; i < NUMSPRITES; ++i)
        ok &= sprite_cache_load_dark_colony(cache, data_root, sprnames[i]);
    for (size_t i = 0; i < sizeof(effect_sprites) / sizeof(*effect_sprites); ++i)
        ok &= sprite_cache_load_dark_colony(cache, data_root, effect_sprites[i]);
    if (map) {
        for (int i = 0; i < map->decoration_count; ++i) {
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].sprite_name);
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].sprite2_name);
            ok &= sprite_cache_load_dark_colony(cache, data_root, map->decorations[i].shadow_name);
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobjtype_t *info = units[i]->info;
        ok &= sprite_cache_load_dark_colony(cache, data_root, units[i]->core.sprite_name);
        ok &= sprite_cache_load_dark_colony(cache, data_root, info ? info->shadow_name : NULL);
    }
    if (!cache->ui) cache->ui = calloc(1, sizeof(*cache->ui));
    if (!cache->ui) return false;
    ok &= load_ui_sprites(data_root, cache->ui);
    return R_BindSprites(cache, &game_info) && ok;
}
