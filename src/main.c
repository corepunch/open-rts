#define _DEFAULT_SOURCE
#include "engine.h"
#include "plugin.h"
#include "renderer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    SDL_Texture *texture;
    int glyph_w;
    int glyph_h;
    int columns;
} DebugFont;

typedef struct {
    bool active;
    char query[64];
    int scroll_y;
    DebugFont font;
} DebugOverlay;

static void debug_font_glyph(char c, uint8_t rows[7]) {
    memset(rows, 0, 7);
    switch (c) {
    case 'A': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1f; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x11; break;
    case 'B': rows[0] = 0x1e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1e; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x1e; break;
    case 'C': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x10; rows[3] = 0x10; rows[4] = 0x10; rows[5] = 0x11; rows[6] = 0x0e; break;
    case 'D': rows[0] = 0x1c; rows[1] = 0x12; rows[2] = 0x11; rows[3] = 0x11; rows[4] = 0x11; rows[5] = 0x12; rows[6] = 0x1c; break;
    case 'E': rows[0] = 0x1f; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x1e; rows[4] = 0x10; rows[5] = 0x10; rows[6] = 0x1f; break;
    case 'F': rows[0] = 0x1f; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x1e; rows[4] = 0x10; rows[5] = 0x10; rows[6] = 0x10; break;
    case 'G': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x10; rows[3] = 0x17; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0e; break;
    case 'H': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1f; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x11; break;
    case 'I': rows[0] = 0x0e; rows[1] = 0x04; rows[2] = 0x04; rows[3] = 0x04; rows[4] = 0x04; rows[5] = 0x04; rows[6] = 0x0e; break;
    case 'J': rows[0] = 0x07; rows[1] = 0x02; rows[2] = 0x02; rows[3] = 0x02; rows[4] = 0x12; rows[5] = 0x12; rows[6] = 0x0c; break;
    case 'K': rows[0] = 0x11; rows[1] = 0x12; rows[2] = 0x14; rows[3] = 0x18; rows[4] = 0x14; rows[5] = 0x12; rows[6] = 0x11; break;
    case 'L': rows[0] = 0x10; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x10; rows[4] = 0x10; rows[5] = 0x10; rows[6] = 0x1f; break;
    case 'M': rows[0] = 0x11; rows[1] = 0x1b; rows[2] = 0x15; rows[3] = 0x15; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x11; break;
    case 'N': rows[0] = 0x11; rows[1] = 0x19; rows[2] = 0x15; rows[3] = 0x13; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x11; break;
    case 'O': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0e; break;
    case 'P': rows[0] = 0x1e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1e; rows[4] = 0x10; rows[5] = 0x10; rows[6] = 0x10; break;
    case 'Q': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11; rows[4] = 0x15; rows[5] = 0x12; rows[6] = 0x0d; break;
    case 'R': rows[0] = 0x1e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x1e; rows[4] = 0x14; rows[5] = 0x12; rows[6] = 0x11; break;
    case 'S': rows[0] = 0x0f; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x0e; rows[4] = 0x01; rows[5] = 0x01; rows[6] = 0x1e; break;
    case 'T': rows[0] = 0x1f; rows[1] = 0x04; rows[2] = 0x04; rows[3] = 0x04; rows[4] = 0x04; rows[5] = 0x04; rows[6] = 0x04; break;
    case 'U': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0e; break;
    case 'V': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x11; rows[4] = 0x11; rows[5] = 0x0a; rows[6] = 0x04; break;
    case 'W': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x15; rows[4] = 0x15; rows[5] = 0x1b; rows[6] = 0x11; break;
    case 'X': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x0a; rows[3] = 0x04; rows[4] = 0x0a; rows[5] = 0x11; rows[6] = 0x11; break;
    case 'Y': rows[0] = 0x11; rows[1] = 0x11; rows[2] = 0x0a; rows[3] = 0x04; rows[4] = 0x04; rows[5] = 0x04; rows[6] = 0x04; break;
    case 'Z': rows[0] = 0x1f; rows[1] = 0x01; rows[2] = 0x02; rows[3] = 0x04; rows[4] = 0x08; rows[5] = 0x10; rows[6] = 0x1f; break;
    case '0': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x13; rows[3] = 0x15; rows[4] = 0x19; rows[5] = 0x11; rows[6] = 0x0e; break;
    case '1': rows[0] = 0x04; rows[1] = 0x0c; rows[2] = 0x04; rows[3] = 0x04; rows[4] = 0x04; rows[5] = 0x04; rows[6] = 0x0e; break;
    case '2': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x01; rows[3] = 0x02; rows[4] = 0x04; rows[5] = 0x08; rows[6] = 0x1f; break;
    case '3': rows[0] = 0x1e; rows[1] = 0x01; rows[2] = 0x01; rows[3] = 0x0e; rows[4] = 0x01; rows[5] = 0x01; rows[6] = 0x1e; break;
    case '4': rows[0] = 0x02; rows[1] = 0x06; rows[2] = 0x0a; rows[3] = 0x12; rows[4] = 0x1f; rows[5] = 0x02; rows[6] = 0x02; break;
    case '5': rows[0] = 0x1f; rows[1] = 0x10; rows[2] = 0x10; rows[3] = 0x1e; rows[4] = 0x01; rows[5] = 0x01; rows[6] = 0x1e; break;
    case '6': rows[0] = 0x06; rows[1] = 0x08; rows[2] = 0x10; rows[3] = 0x1e; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0e; break;
    case '7': rows[0] = 0x1f; rows[1] = 0x01; rows[2] = 0x02; rows[3] = 0x04; rows[4] = 0x08; rows[5] = 0x08; rows[6] = 0x08; break;
    case '8': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x0e; rows[4] = 0x11; rows[5] = 0x11; rows[6] = 0x0e; break;
    case '9': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x11; rows[3] = 0x0f; rows[4] = 0x01; rows[5] = 0x02; rows[6] = 0x0c; break;
    case '/': rows[0] = 0x01; rows[1] = 0x02; rows[2] = 0x04; rows[3] = 0x08; rows[4] = 0x10; break;
    case '.': rows[6] = 0x04; break;
    case '-': rows[3] = 0x1f; break;
    case '_': rows[6] = 0x1f; break;
    case ':': rows[2] = 0x04; rows[4] = 0x04; break;
    case ',': rows[5] = 0x04; rows[6] = 0x08; break;
    case '+': rows[2] = 0x04; rows[3] = 0x1f; rows[4] = 0x04; break;
    case '=': rows[2] = 0x1f; rows[4] = 0x1f; break;
    case '(': rows[0] = 0x02; rows[1] = 0x04; rows[2] = 0x08; rows[3] = 0x08; rows[4] = 0x08; rows[5] = 0x04; rows[6] = 0x02; break;
    case ')': rows[0] = 0x08; rows[1] = 0x04; rows[2] = 0x02; rows[3] = 0x02; rows[4] = 0x02; rows[5] = 0x04; rows[6] = 0x08; break;
    case '?': rows[0] = 0x0e; rows[1] = 0x11; rows[2] = 0x01; rows[3] = 0x02; rows[4] = 0x04; rows[6] = 0x04; break;
    case '!': rows[0] = 0x04; rows[1] = 0x04; rows[2] = 0x04; rows[3] = 0x04; rows[4] = 0x04; rows[6] = 0x04; break;
    case ' ': break;
    default: rows[0] = 0x1f; rows[1] = 0x01; rows[2] = 0x02; rows[3] = 0x04; rows[4] = 0x08; rows[5] = 0x00; rows[6] = 0x04; break;
    }
}

static bool debug_font_init(SDL_Renderer *renderer, DebugFont *font) {
    if (!renderer || !font) return false;
    memset(font, 0, sizeof(*font));
    font->glyph_w = 6;
    font->glyph_h = 8;
    font->columns = 16;
    int rows = 6;
    int atlas_w = font->columns * font->glyph_w;
    int atlas_h = rows * font->glyph_h;
    uint32_t *pixels = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!pixels) return false;

    for (int ch = 32; ch < 128; ++ch) {
        uint8_t glyph[7];
        char mapped = (char)ch;
        if (mapped >= 'a' && mapped <= 'z') mapped = (char)toupper((unsigned char)mapped);
        debug_font_glyph(mapped, glyph);
        int idx = ch - 32;
        int gx = (idx % font->columns) * font->glyph_w;
        int gy = (idx / font->columns) * font->glyph_h;
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 5; ++x) {
                if (glyph[y] & (1u << (4 - x))) {
                    pixels[(gy + y) * atlas_w + gx + x] = 0xffffffffu;
                }
            }
        }
    }

    font->texture = rgba_texture(renderer, pixels, atlas_w, atlas_h, true);
    free(pixels);
    return font->texture != NULL;
}

static void debug_font_destroy(DebugFont *font) {
    if (!font) return;
    if (font->texture) SDL_DestroyTexture(font->texture);
    memset(font, 0, sizeof(*font));
}

static void debug_font_draw_text(SDL_Renderer *renderer, const DebugFont *font, int x, int y,
                                 const char *text, SDL_Color color, int scale) {
    if (!renderer || !font || !font->texture || !text || scale <= 0) return;
    SDL_SetTextureColorMod(font->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font->texture, color.a);
    int cx = x;
    int cy = y;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\n') {
            cy += font->glyph_h * scale;
            cx = x;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)toupper(ch);
        if (ch < 32 || ch >= 128) ch = '?';
        int idx = (int)ch - 32;
        SDL_Rect src = {
            (idx % font->columns) * font->glyph_w,
            (idx / font->columns) * font->glyph_h,
            font->glyph_w,
            font->glyph_h,
        };
        SDL_Rect dst = { cx, cy, font->glyph_w * scale, font->glyph_h * scale };
        SDL_RenderCopy(renderer, font->texture, &src, &dst);
        cx += font->glyph_w * scale;
    }
}

static void debug_append_ints(char *dst, size_t dst_size, const int *values, int count) {
    size_t len = strlen(dst);
    for (int i = 0; i < count && len + 8 < dst_size; ++i) {
        int written = snprintf(dst + len, dst_size - len, "%s%d", i == 0 ? "" : " ", values[i]);
        if (written < 0) break;
        len += (size_t)written;
    }
}

static int debug_sprite_id_for_name(const RtsGameInfo *game_info, const char *sprite_name) {
    if (!game_info || !game_info->sprnames || !sprite_name) return -1;
    const char *sprite_base = strrchr(sprite_name, '/');
    sprite_base = sprite_base ? sprite_base + 1 : sprite_name;
    for (int i = 0; i < game_info->sprite_count; ++i) {
        const char *name = game_info->sprnames[i];
        if (!name) continue;
        if (strcasecmp(name, sprite_name) == 0) return i;
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        if (strcasecmp(base, sprite_base) == 0) return i;
    }
    return -1;
}

static int debug_count_states_for_sprite(const RtsGameInfo *game_info, int sprite_id) {
    if (!game_info || !game_info->states || sprite_id < 0) return 0;
    int count = 0;
    for (int i = 0; i < game_info->state_count; ++i) {
        if (game_info->states[i].sprite == sprite_id) count++;
    }
    return count;
}

static void debug_overlay_render(const App *app, const SpriteSheet *sprite, const char *sprite_name,
                                 const RtsGameInfo *game_info, const DebugOverlay *overlay) {
    if (!app || !app->renderer || !sprite || !sprite->texture || !overlay || !overlay->font.texture) return;

    SDL_SetRenderDrawColor(app->renderer, 10, 12, 16, 255);
    SDL_RenderClear(app->renderer);

    SDL_Color white = { 236, 240, 244, 255 };
    SDL_Color cyan = { 98, 224, 161, 255 };
    SDL_Color gray = { 180, 190, 196, 255 };

    char line[512];
    snprintf(line, sizeof(line), "DEBUG OVERLAY %s", overlay->query);
    debug_font_draw_text(app->renderer, &overlay->font, 16, 14, line, white, 2);

    int debug_sprite_id = debug_sprite_id_for_name(game_info, sprite_name);
    int state_count = debug_count_states_for_sprite(game_info, debug_sprite_id);
    snprintf(line, sizeof(line), "%s  FRAMES %d  SEQUENCES %d  STATES %d",
             sprite_name ? sprite_name : "(UNKNOWN)", sprite->frame_count,
             sprite->sequence_count, state_count);
    debug_font_draw_text(app->renderer, &overlay->font, 16, 34, line, gray, 1);

    int info_y = 52;
    for (int i = 0; i < sprite->sequence_count && i < 4; ++i) {
        const SpriteSequence *seq = &sprite->sequences[i];
        char starts[256] = { 0 };
        char dirs[256] = { 0 };
        char name[32];
        snprintf(name, sizeof(name), "%s", seq->name);
        for (char *p = name; *p; ++p) *p = (char)toupper((unsigned char)*p);
        snprintf(starts, sizeof(starts), "IDX");
        snprintf(dirs, sizeof(dirs), "DIR");
        debug_append_ints(starts, sizeof(starts), seq->frame_starts, seq->facings);
        debug_append_ints(dirs, sizeof(dirs), seq->direction_codes, seq->facings);

        snprintf(line, sizeof(line), "%s  %s", name, dirs);
        debug_font_draw_text(app->renderer, &overlay->font, 16, info_y, line, cyan, 1);
        info_y += 10;

        snprintf(line, sizeof(line), "   %s", starts);
        debug_font_draw_text(app->renderer, &overlay->font, 16, info_y, line, gray, 1);
        info_y += 10;
    }
    if (game_info && game_info->states && debug_sprite_id >= 0) {
        int shown = 0;
        for (int i = 0; i < game_info->state_count && shown < 8; ++i) {
            const RtsState *state = &game_info->states[i];
            if (state->sprite != debug_sprite_id) continue;
            snprintf(line, sizeof(line), "S%03d G%d T%d N%d F%d",
                     i, state->misc1, state->tics, state->nextstate, state->frame);
            debug_font_draw_text(app->renderer, &overlay->font, 16, info_y, line, cyan, 1);
            info_y += 10;
            if (state->facings > 0) {
                char dirs[256] = "DIR";
                char frames[256] = "FRM";
                debug_append_ints(dirs, sizeof(dirs), state->direction_codes, state->facings);
                debug_append_ints(frames, sizeof(frames), state->facing_frames, state->facings);
                snprintf(line, sizeof(line), "   %s", dirs);
                debug_font_draw_text(app->renderer, &overlay->font, 16, info_y, line, gray, 1);
                info_y += 10;
                snprintf(line, sizeof(line), "   %s", frames);
                debug_font_draw_text(app->renderer, &overlay->font, 16, info_y, line, gray, 1);
                info_y += 10;
            }
            shown++;
        }
    }

    int cols = 8;
    int frame_scale = 2;
    int available_w = app->win_w - 32;
    while (frame_scale > 1 && cols * (sprite->frame_w * frame_scale + 22) > available_w) {
        frame_scale--;
    }
    int frame_w = sprite->frame_w * frame_scale;
    int frame_h = sprite->frame_h * frame_scale;
    int start_x = 16;
    int start_y = info_y + 8;
    int cell_w = frame_w + 22;
    int cell_h = frame_h + 18;
    int rows = sprite->frame_count > 0 ? (sprite->frame_count + cols - 1) / cols : 0;
    int content_h = rows * cell_h;

    SDL_SetRenderDrawColor(app->renderer, 18, 21, 27, 255);
    SDL_Rect panel = { 10, start_y - 6, app->win_w - 20, app->win_h - start_y - 14 };
    SDL_RenderFillRect(app->renderer, &panel);

    int viewport_y = start_y - 6;
    int viewport_h = app->win_h - start_y - 14;
    int max_scroll = content_h > viewport_h ? content_h - viewport_h : 0;
    int scroll_y = overlay->scroll_y;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    SDL_Rect viewport = { 10, viewport_y, app->win_w - 20, viewport_h };
    SDL_Rect old_clip = { 0, 0, 0, 0 };
    SDL_RenderGetClipRect(app->renderer, &old_clip);
    SDL_RenderSetClipRect(app->renderer, &viewport);

    for (int i = 0; i < sprite->frame_count; ++i) {
        int row = i / cols;
        int col = i % cols;
        int x = start_x + col * cell_w;
        int y = start_y + row * cell_h - scroll_y;
        if (y + cell_h < viewport_y || y > viewport_y + viewport_h) continue;
        SDL_Rect dst = { x, y, frame_w, frame_h };
        SDL_RenderCopy(app->renderer, sprite->texture, &sprite->frames[i], &dst);
        snprintf(line, sizeof(line), "%d", i);
        debug_font_draw_text(app->renderer, &overlay->font, x, y + frame_h + 2, line, white, 1);
    }

    SDL_RenderSetClipRect(app->renderer, &old_clip);

    SDL_Rect border = { 10, 10, app->win_w - 20, app->win_h - 20 };
    SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
    SDL_RenderDrawRect(app->renderer, &border);
}

static const RtsActorType *plugin_actor_type_by_id(const RtsPlugin *plugin, uint16_t type_id) {
    if (!plugin || !plugin->actor_types) return NULL;
    for (int i = 0; i < plugin->actor_type_count; ++i) {
        if (plugin->actor_types[i].id == type_id) return &plugin->actor_types[i];
    }
    return NULL;
}

static const RtsActorType *plugin_actor_type_for_unit(const RtsPlugin *plugin, const Unit *unit) {
    const RtsActorType *type = plugin_actor_type_by_id(plugin, unit ? unit->type_id : 0);
    if (type) return type;
    if (!plugin || !plugin->actor_types || !unit) return NULL;
    for (int i = 0; i < plugin->actor_type_count; ++i) {
        const char *sprite = plugin->actor_types[i].sprite_name;
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->sprite_name) == 0) {
            return &plugin->actor_types[i];
        }
    }
    return plugin->actor_type_count > 0 ? &plugin->actor_types[0] : NULL;
}

static void apply_actor_type_defaults(Unit *unit, const RtsActorType *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack_range <= 0.0f) unit->attack_range = type->attack_range;
    if (unit->attack_damage <= 0) unit->attack_damage = type->attack_damage;
    if (unit->attack_cooldown_ms <= 0) unit->attack_cooldown_ms = type->attack_cooldown_ms;
    if (unit->attack_anim_ms <= 0) unit->attack_anim_ms = type->attack_anim_ms;
    if (unit->death_anim_ms <= 0) unit->death_anim_ms = type->death_anim_ms;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->sprite_name[0] == '\0' && type->sprite_name) {
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", type->sprite_name);
    }
    if (unit->shadow_name[0] == '\0' && type->shadow_name) {
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    }
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name) {
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name), "%s", type->muzzle_flash_name);
    }
}

static void apply_plugin_actor_defaults(const RtsPlugin *plugin, Unit *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        apply_actor_type_defaults(&units[i], plugin_actor_type_for_unit(plugin, &units[i]));
        rts_apply_mobjinfo_defaults(plugin ? plugin->game_info : NULL, &units[i]);
    }
}

static bool spawn_debug_enemy_unit(const RtsPlugin *plugin, const GameMap *map, const App *app,
                                   Unit *units, int *unit_count, int sx, int sy) {
    if (!plugin || !map || !app || !units || !unit_count || *unit_count >= MAX_UNITS) return false;
    const RtsActorType *type = plugin_actor_type_by_id(plugin, plugin->debug_enemy_type_id);
    if (!type && plugin->actor_type_count > 0) type = &plugin->actor_types[0];
    if (!type) return false;
    Cell cell = screen_to_grid(app, sx, sy);
    if (!map_contains(map, cell.x, cell.y)) return false;
    Unit *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    unit->gx = (float)cell.x + 0.5f;
    unit->gy = (float)cell.y + 0.5f;
    unit->owner = 1;
    unit->facing_code = plugin->game_info ? 6 : 8;
    apply_actor_type_defaults(unit, type);
    rts_apply_mobjinfo_defaults(plugin->game_info, unit);
    (*unit_count)++;
    return true;
}

static void load_plugin_by_id(const char *game_id) {
    char lib_path[1024];
    /* Try native extension first, then fall back to alternatives */
    const char *extensions[] = { ".dylib", ".so", NULL };
    for (int i = 0; extensions[i]; ++i) {
        snprintf(lib_path, sizeof(lib_path), "build/libs/%s%s", game_id, extensions[i]);
        if (rts_plugin_load(lib_path)) return;
    }
}

int main(int argc, char **argv) {
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : (screenshot_only ? 3 : 1);
    int render_scale = 1;
    const char *debug_query = NULL;
    const char *game_id = "dark-reign";
    while (argc > arg_base) {
        if (argc > arg_base + 1 && strcmp(argv[arg_base], "--game") == 0) {
            game_id = argv[arg_base + 1];
            arg_base += 2;
        } else if (strncmp(argv[arg_base], "--game=", 7) == 0) {
            game_id = argv[arg_base] + 7;
            arg_base += 1;
        } else if (argc > arg_base + 1 && strcmp(argv[arg_base], "--scale") == 0) {
            render_scale = atoi(argv[arg_base + 1]);
            if (render_scale < 1) render_scale = 1;
            if (render_scale > 6) render_scale = 6;
            arg_base += 2;
        } else if (strncmp(argv[arg_base], "--debug=", 8) == 0) {
            debug_query = argv[arg_base] + 8;
            arg_base += 1;
        } else if (strncmp(argv[arg_base], "-debug=", 7) == 0) {
            debug_query = argv[arg_base] + 7;
            arg_base += 1;
        } else if ((strcmp(argv[arg_base], "--debug") == 0 || strcmp(argv[arg_base], "-debug") == 0) &&
                   argc > arg_base + 1) {
            debug_query = argv[arg_base + 1];
            arg_base += 2;
        } else {
            break;
        }
    }

    load_plugin_by_id(game_id);
    const RtsPlugin *plugin = rts_find_plugin(game_id);
    if (!plugin) {
        fprintf(stderr, "unknown game '%s' (could not load build/libs/%s.so)\n", game_id, game_id);
        return 1;
    }

    const char *data_root = argc > arg_base ? argv[arg_base] : plugin->default_root;
    const char *map_rel_or_abs = argc > arg_base + 1 ? argv[arg_base + 1] : plugin->default_map;
    const char *sprite_name = argc > arg_base + 2 ? argv[arg_base + 2] : plugin->default_sprite;
    char debug_sprite_name[1024];
    if (debug_query && debug_query[0] != '\0') {
        if (strchr(debug_query, '/') || strchr(debug_query, '.')) {
            snprintf(debug_sprite_name, sizeof(debug_sprite_name), "%s", debug_query);
        } else if (plugin && strcmp(plugin->id, "dark-colony") == 0) {
            snprintf(debug_sprite_name, sizeof(debug_sprite_name), "SPRITES/%s.SPR", debug_query);
        } else {
            snprintf(debug_sprite_name, sizeof(debug_sprite_name), "%s", debug_query);
        }
        sprite_name = debug_sprite_name;
    }

    char map_path[1024];
    if (map_rel_or_abs[0] == '/') {
        snprintf(map_path, sizeof(map_path), "%s", map_rel_or_abs);
    } else {
        path_join(map_path, sizeof(map_path), data_root, map_rel_or_abs);
    }

    RtsRenderer renderer;
    App app = { 0 };
    app.win_w = 1280;
    app.win_h = 800;
    app.render_scale = render_scale;
    app.show_grid = false;
    app.running = true;
    if (!rts_renderer_create(&renderer, rts_sdl_renderer_backend(), "open-rts - paletted RTS base",
                             app.win_w, app.win_h, check_only || screenshot_only,
                             check_only || screenshot_only)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;
    refresh_app_viewport(&app);

    GameMap map;
    if (!plugin->load_map(map_path, &map)) {
        rts_renderer_destroy(&renderer);
        return 1;
    }

    Tileset tileset;
    SpriteSheet unit_sprite;
    DebugOverlay debug_overlay = { 0 };
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!plugin->load_assets(app.renderer, data_root, &map, sprite_name, &tileset, &unit_sprite)) {
        destroy_map(&map);
        rts_renderer_destroy(&renderer);
        return 1;
    }
    if (debug_query && debug_query[0] != '\0') {
        debug_overlay.active = true;
        snprintf(debug_overlay.query, sizeof(debug_overlay.query), "%s", debug_query);
        debug_overlay.scroll_y = 0;
        if (!debug_font_init(app.renderer, &debug_overlay.font)) {
            fprintf(stderr, "warning: failed to create debug font overlay\n");
            debug_overlay.active = false;
        }
    }
    app.cell_w = plugin->cell_w > 0 ? plugin->cell_w : (tileset.tile_w > 0 ? tileset.tile_w : CELL_W);
    app.cell_h = plugin->cell_h > 0 ? plugin->cell_h : (tileset.tile_h > 0 ? tileset.tile_h : CELL_H);

    Unit units[MAX_UNITS] = { 0 };
    int unit_count = plugin->load_initial_units ? plugin->load_initial_units(map_path, units, MAX_UNITS) : 0;
    if (unit_count <= 0) {
        unit_count = 6;
        int cx = map.width / 2;
        int cy = map.height / 2;
        const RtsActorType *fallback_type = plugin->actor_type_count > 0 ? &plugin->actor_types[0] : NULL;
        for (int i = 0; i < unit_count; ++i) {
            units[i].gx = (float)(cx + i % 3) + 0.5f;
            units[i].gy = (float)(cy + i / 3) + 0.5f;
            units[i].speed = 5.5f;
            units[i].owner = 0;
            units[i].selected = i == 0;
            if (fallback_type) {
                apply_actor_type_defaults(&units[i], fallback_type);
            } else {
                units[i].traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE;
                snprintf(units[i].sprite_name, sizeof(units[i].sprite_name), "%s", sprite_name);
            }
        }
    }
    apply_plugin_actor_defaults(plugin, units, unit_count);
    RtsVisualEffect effects[MAX_VISUAL_EFFECTS] = { 0 };

    SpriteCache decoration_sprites = { 0 };
    if (plugin->load_runtime_sprites &&
        !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                      &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", plugin->name);
    }

    float focus_gx = unit_count > 0 ? units[0].gx : (float)map.width * 0.5f;
    float focus_gy = unit_count > 0 ? units[0].gy : (float)map.height * 0.5f;
    float sx, sy;
    grid_to_screen(&app, focus_gx, focus_gy, &sx, &sy);
    app.cam_x = (float)app.win_w * 0.5f - sx;
    app.cam_y = (float)app.win_h * 0.5f - sy;

    printf("Loaded %s (%dx%d, tileset %s, scale %dx, %d units, %d map decorations). Controls: left select/drag, right move, Alt+left spawn enemy, WASD/arrows pan, G grid, Ctrl+A select all.\n",
           map_path, map.width, map.height, map.tileset_name, app.render_scale, unit_count, map.decoration_count);

    if (debug_overlay.active) {
        if (screenshot_only) {
            rts_renderer_begin_frame(&renderer, (SDL_Color){ 10, 12, 16, 255 });
            debug_overlay_render(&app, &unit_sprite, sprite_name,
                                 plugin->game_info, &debug_overlay);
            if (rts_renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
            debug_font_destroy(&debug_overlay.font);
            destroy_sprite_cache(&decoration_sprites);
            destroy_sprite(&unit_sprite);
            destroy_tileset(&tileset);
            destroy_map(&map);
            rts_renderer_destroy(&renderer);
            return 0;
        }
        while (app.running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    app.running = false;
                } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    app.running = false;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    int wheel = e.wheel.y;
                    if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wheel = -wheel;
                    debug_overlay.scroll_y -= wheel * 32;
                    if (debug_overlay.scroll_y < 0) debug_overlay.scroll_y = 0;
                }
            }
            rts_renderer_begin_frame(&renderer, (SDL_Color){ 10, 12, 16, 255 });
            debug_overlay_render(&app, &unit_sprite, sprite_name,
                                 plugin->game_info, &debug_overlay);
            rts_renderer_end_frame(&renderer);
            SDL_Delay(16);
        }
        debug_font_destroy(&debug_overlay.font);
        destroy_sprite_cache(&decoration_sprites);
        destroy_sprite(&unit_sprite);
        destroy_tileset(&tileset);
        destroy_map(&map);
        rts_renderer_destroy(&renderer);
        return 0;
    }

    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            rts_renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            render_map(&app, &map, &tileset);
            render_decorations(&app, &map, &decoration_sprites);
            render_units(&app, units, unit_count, &unit_sprite, &decoration_sprites,
                         plugin->game_info, SDL_GetTicks());
            render_visual_effects(&app, effects, MAX_VISUAL_EFFECTS,
                                  &decoration_sprites, plugin->game_info);
            if (rts_renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s.\n",
               tileset.count, unit_sprite.frame_count, sprite_name);
        destroy_sprite_cache(&decoration_sprites);
        destroy_sprite(&unit_sprite);
        destroy_tileset(&tileset);
        destroy_map(&map);
        rts_renderer_destroy(&renderer);
        return 0;
    }

    uint64_t prev = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    float accumulator = 0.0f;

    while (app.running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float frame_dt = (float)((double)(now - prev) / freq);
        if (frame_dt > 0.25f) frame_dt = 0.25f;
        prev = now;
        accumulator += frame_dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                (SDL_GetModState() & KMOD_ALT) != 0) {
                if (spawn_debug_enemy_unit(plugin, &map, &app, units, &unit_count, e.button.x, e.button.y)) {
                    if (plugin->load_runtime_sprites &&
                        !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                                      &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load debug enemy sprite\n");
                    }
                }
                continue;
            }
            handle_event(&app, &map, units, unit_count, &e);
        }
        update_camera_from_keyboard(&app, frame_dt);
        while (accumulator >= FIXED_DT) {
            update_units(&map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                         plugin->game_info, FIXED_DT);
            update_visual_effects(&map, effects, MAX_VISUAL_EFFECTS,
                                  plugin->game_info, FIXED_DT);
            accumulator -= FIXED_DT;
        }

        app.ticks_ms = SDL_GetTicks();
        rts_renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        render_map(&app, &map, &tileset);
        render_decorations(&app, &map, &decoration_sprites);
        render_units(&app, units, unit_count, &unit_sprite, &decoration_sprites,
                     plugin->game_info, SDL_GetTicks());
        render_visual_effects(&app, effects, MAX_VISUAL_EFFECTS,
                              &decoration_sprites, plugin->game_info);
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        rts_renderer_end_frame(&renderer);
    }

    destroy_sprite_cache(&decoration_sprites);
    destroy_sprite(&unit_sprite);
    destroy_tileset(&tileset);
    destroy_map(&map);
    rts_renderer_destroy(&renderer);
    return 0;
}
