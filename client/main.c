#define _DEFAULT_SOURCE
#include "engine.h"
#include "game_ui.h"
#include "plugin.h"
#include "renderer.h"

#include <ctype.h>
#include <math.h>
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
    bool animation_grid;
    char query[64];
    int scroll_y;
    DebugFont font;
} DebugOverlay;

typedef struct {
    char name[16];
    int state_ids[96];
    int state_count;
    bool loop;
} DebugAnimRow;

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

static int debug_sprite_id_for_name(const GameInfo *game_info, const char *sprite_name) {
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

static int debug_count_states_for_sprite(const GameInfo *game_info, int sprite_id) {
    if (!game_info || !game_info->states || sprite_id < 0) return 0;
    int count = 0;
    for (int i = 0; i < game_info->state_count; ++i) {
        if (game_info->states[i].sprite == sprite_id) count++;
    }
    return count;
}

static bool debug_row_has_state(const DebugAnimRow *row, int state_id) {
    if (!row) return false;
    for (int i = 0; i < row->state_count; ++i) {
        if (row->state_ids[i] == state_id) return true;
    }
    return false;
}

static bool debug_row_start_exists(const DebugAnimRow *rows, int row_count, int start_state) {
    for (int i = 0; i < row_count; ++i) {
        if (rows[i].state_count > 0 && rows[i].state_ids[0] == start_state) return true;
    }
    return false;
}

static bool debug_state_matches_sprite(const GameInfo *game_info, int state_id, int sprite_id) {
    if (!game_info || !game_info->states || state_id == game_info->null_state ||
        state_id < 0 || state_id >= game_info->state_count) {
        return false;
    }
    return game_info->states[state_id].sprite == sprite_id;
}

static bool debug_add_anim_row(const GameInfo *game_info, int sprite_id, const char *name,
                               int start_state, DebugAnimRow *rows, int *row_count, int max_rows) {
    if (!game_info || !game_info->states || !rows || !row_count || *row_count >= max_rows) return false;
    if (!debug_state_matches_sprite(game_info, start_state, sprite_id)) return false;
    if (debug_row_start_exists(rows, *row_count, start_state)) return false;

    const State *first = &game_info->states[start_state];
    int group = first->misc1;
    DebugAnimRow row;
    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%s", name);

    int state_id = start_state;
    while (row.state_count < (int)(sizeof(row.state_ids) / sizeof(row.state_ids[0]))) {
        if (!debug_state_matches_sprite(game_info, state_id, sprite_id)) break;
        const State *state = &game_info->states[state_id];
        if (state->misc1 != group) break;
        if (debug_row_has_state(&row, state_id)) {
            row.loop = true;
            break;
        }
        row.state_ids[row.state_count++] = state_id;
        int next = state->nextstate;
        if (next == game_info->null_state || next < 0 || next >= game_info->state_count) break;
        if (debug_row_has_state(&row, next)) {
            row.loop = true;
            break;
        }
        state_id = next;
    }

    if (row.state_count <= 0) return false;
    rows[*row_count] = row;
    (*row_count)++;
    return true;
}

static int debug_collect_anim_rows(const GameInfo *game_info, int sprite_id,
                                   DebugAnimRow *rows, int max_rows) {
    if (!game_info || !game_info->mobjinfo || !rows || max_rows <= 0) return 0;
    int row_count = 0;
    for (int i = 0; i < game_info->mobj_type_count; ++i) {
        const MobjInfo *info = &game_info->mobjinfo[i];
        debug_add_anim_row(game_info, sprite_id, "STAND", info->spawnstate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "RUN", info->seestate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "ATTACK", info->missilestate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "MELEE", info->meleestate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "PAIN", info->painstate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "DEATH", info->deathstate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "XDEATH", info->xdeathstate, rows, &row_count, max_rows);
        debug_add_anim_row(game_info, sprite_id, "RAISE", info->raisestate, rows, &row_count, max_rows);
    }
    return row_count;
}

static int debug_state_facing_slot(const State *state, int facing_code) {
    if (!state || state->facings <= 0) return -1;
    int best = 0;
    int best_delta = 1000000;
    for (int i = 0; i < state->facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = state->direction_codes[i];
        int delta = abs(code - facing_code);
        if (code <= 7 && facing_code <= 7) {
            int wrapped = 8 - delta;
            if (wrapped < delta) delta = wrapped;
        }
        if (delta < best_delta) {
            best_delta = delta;
            best = i;
        }
    }
    return best;
}

static void debug_resolve_state_frame(const State *state, int facing_code,
                                      int *frame_out, uint32_t *flags_out) {
    int frame = state ? state->frame : 0;
    uint32_t flags = state ? state->flags : 0;
    if (state && state->facings > 0) {
        int slot = debug_state_facing_slot(state, facing_code);
        if (slot >= 0) {
            frame = state->facing_frames[slot];
            flags = state->facing_flags[slot];
        }
    }
    if (frame_out) *frame_out = frame;
    if (flags_out) *flags_out = flags;
}

static const State *debug_anim_state_for_time(const GameInfo *game_info,
                                                 const DebugAnimRow *row, uint32_t ticks_ms) {
    if (!game_info || !game_info->states || !row || row->state_count <= 0) return NULL;
    int total_ms = 0;
    for (int i = 0; i < row->state_count; ++i) {
        const State *state = &game_info->states[row->state_ids[i]];
        int tics = state->tics > 0 ? state->tics : 8;
        total_ms += (tics * 1000) / 30;
    }
    if (total_ms <= 0) return &game_info->states[row->state_ids[0]];
    int cursor = row->loop ? (int)(ticks_ms % (uint32_t)total_ms) : (int)(ticks_ms % (uint32_t)total_ms);
    for (int i = 0; i < row->state_count; ++i) {
        const State *state = &game_info->states[row->state_ids[i]];
        int tics = state->tics > 0 ? state->tics : 8;
        int duration = (tics * 1000) / 30;
        if (cursor < duration) return state;
        cursor -= duration;
    }
    return &game_info->states[row->state_ids[row->state_count - 1]];
}

static void debug_animation_grid_render(const App *app, const SpriteSheet *sprite,
                                        const char *sprite_name, const GameInfo *game_info,
                                        const DebugOverlay *overlay) {
    if (!app || !app->renderer || !sprite || !sprite->texture || !overlay || !overlay->font.texture) return;

    SDL_SetRenderDrawColor(app->renderer, 10, 12, 16, 255);
    SDL_RenderClear(app->renderer);

    SDL_Color white = { 236, 240, 244, 255 };
    SDL_Color cyan = { 98, 224, 161, 255 };
    SDL_Color gray = { 180, 190, 196, 255 };
    SDL_Color dim = { 82, 92, 104, 255 };

    int sprite_id = debug_sprite_id_for_name(game_info, sprite_name);
    DebugAnimRow rows[16];
    int row_count = debug_collect_anim_rows(game_info, sprite_id, rows, (int)(sizeof(rows) / sizeof(rows[0])));

    char line[512];
    snprintf(line, sizeof(line), "ANIMATION DEBUG %s", overlay->query);
    debug_font_draw_text(app->renderer, &overlay->font, 16, 14, line, white, 2);
    snprintf(line, sizeof(line), "%s  FRAMES %d  ROWS %d",
             sprite_name ? sprite_name : "(UNKNOWN)", sprite->frame_count, row_count);
    debug_font_draw_text(app->renderer, &overlay->font, 16, 36, line, gray, 1);

    int label_w = 76;
    int cols = 8;
    int gap = 8;
    int available_w = app->win_w - 32 - label_w - gap * (cols - 1);
    int cell_w = available_w / cols;
    if (cell_w < 48) cell_w = 48;
    int scale = 1;
    while ((scale + 1) * sprite->frame_w <= cell_w - 12 && (scale + 1) * sprite->frame_h <= 104) {
        scale++;
    }
    if (scale < 1) scale = 1;
    int draw_w = sprite->frame_w * scale;
    int draw_h = sprite->frame_h * scale;
    int cell_h = draw_h + 34;
    if (cell_h < 72) cell_h = 72;
    int start_x = 16;
    int start_y = 64;
    int content_h = row_count * cell_h;
    int viewport_h = app->win_h - start_y - 18;
    int max_scroll = content_h > viewport_h ? content_h - viewport_h : 0;
    int scroll_y = overlay->scroll_y;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    for (int dir = 0; dir < cols; ++dir) {
        snprintf(line, sizeof(line), "DIR%d", dir);
        int x = start_x + label_w + dir * (cell_w + gap);
        debug_font_draw_text(app->renderer, &overlay->font, x + 4, start_y - 14, line, cyan, 1);
    }

    SDL_Rect viewport = { 10, start_y - 4, app->win_w - 20, viewport_h + 8 };
    SDL_Rect old_clip = { 0, 0, 0, 0 };
    SDL_RenderGetClipRect(app->renderer, &old_clip);
    SDL_RenderSetClipRect(app->renderer, &viewport);

    if (row_count <= 0) {
        debug_font_draw_text(app->renderer, &overlay->font, 16, start_y + 12,
                             "NO STATE ANIMATIONS FOUND FOR THIS SPRITE", gray, 1);
    }

    for (int row_idx = 0; row_idx < row_count; ++row_idx) {
        const DebugAnimRow *row = &rows[row_idx];
        int y = start_y + row_idx * cell_h - scroll_y;
        if (y + cell_h < viewport.y || y > viewport.y + viewport.h) continue;

        snprintf(line, sizeof(line), "%s", row->name);
        debug_font_draw_text(app->renderer, &overlay->font, start_x, y + 8, line, white, 1);
        snprintf(line, sizeof(line), "%d STEPS%s", row->state_count, row->loop ? " LOOP" : "");
        debug_font_draw_text(app->renderer, &overlay->font, start_x, y + 20, line, dim, 1);

        const State *state = debug_anim_state_for_time(game_info, row, SDL_GetTicks());
        for (int dir = 0; dir < cols; ++dir) {
            int x = start_x + label_w + dir * (cell_w + gap);
            SDL_Rect cell = { x, y, cell_w, cell_h - 6 };
            SDL_SetRenderDrawColor(app->renderer, 18, 21, 27, 255);
            SDL_RenderFillRect(app->renderer, &cell);
            SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
            SDL_RenderDrawRect(app->renderer, &cell);

            int frame = 0;
            uint32_t flags = 0;
            debug_resolve_state_frame(state, dir, &frame, &flags);
            if (frame < 0 || frame >= sprite->frame_count) continue;

            SDL_Rect dst = {
                x + (cell_w - draw_w) / 2,
                y + 18 + ((cell_h - 28) - draw_h) / 2,
                draw_w,
                draw_h,
            };
            SDL_RendererFlip flip = (flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
            snprintf(line, sizeof(line), "F%d%s", frame, (flags & RTS_FRAME_FLIP_X) ? " M" : "");
            debug_font_draw_text(app->renderer, &overlay->font, x + 4, y + 4, line, gray, 1);
        }
    }

    SDL_RenderSetClipRect(app->renderer, &old_clip);

    SDL_Rect border = { 10, 10, app->win_w - 20, app->win_h - 20 };
    SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
    SDL_RenderDrawRect(app->renderer, &border);
}

static void debug_overlay_render(const App *app, const SpriteSheet *sprite, const char *sprite_name,
                                 const GameInfo *game_info, const DebugOverlay *overlay) {
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
            const State *state = &game_info->states[i];
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

static const ActorType *plugin_actor_type_by_id(const Plugin *plugin, uint16_t type_id) {
    if (!plugin || !plugin->actor_types) return NULL;
    for (int i = 0; i < plugin->actor_type_count; ++i) {
        if (plugin->actor_types[i].id == type_id) return &plugin->actor_types[i];
    }
    return NULL;
}

static const ActorType *plugin_actor_type_for_unit(const Plugin *plugin, const Unit *unit) {
    const ActorType *type = plugin_actor_type_by_id(plugin, unit ? unit->type_id : 0);
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

static void apply_actor_type_defaults(Unit *unit, const ActorType *type) {
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
    if (unit->harvest_state_id <= 0) unit->harvest_state_id = type->harvest_state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->render_intensity == 0) unit->render_intensity = 16;
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->harvest_target == 0) unit->harvest_target = -1;
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

static void apply_plugin_actor_defaults(const Plugin *plugin, Unit *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        apply_actor_type_defaults(&units[i], plugin_actor_type_for_unit(plugin, &units[i]));
        apply_mobjinfo_defaults(plugin ? plugin->game_info : NULL, &units[i]);
    }
}

static bool spawn_debug_enemy_unit(const Plugin *plugin, const GameMap *map, const App *app,
                                   Unit *units, int *unit_count, int sx, int sy) {
    if (!plugin || !map || !app || !units || !unit_count || *unit_count >= MAX_UNITS) return false;
    const ActorType *type = plugin_actor_type_by_id(plugin, plugin->debug_enemy_type_id);
    if (!type && plugin->actor_type_count > 0) type = &plugin->actor_types[0];
    if (!type) return false;
    Cell cell = screen_to_map_grid(app, map, sx, sy);
    if (!map_contains(map, cell.x, cell.y)) return false;
    Unit *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    unit->gx = (float)cell.x + 0.5f;
    unit->gy = (float)cell.y + 0.5f;
    unit->owner = 1;
    unit->facing_code = plugin->game_info ? 6 : 8;
    apply_actor_type_defaults(unit, type);
    apply_mobjinfo_defaults(plugin->game_info, unit);
    (*unit_count)++;
    return true;
}

static bool focus_camera_on_first_player_unit(App *app, const GameMap *map,
                                              const Unit *units, int unit_count) {
    if (!app || !map || !units) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        float sx = 0.0f, sy = 0.0f;
        map_grid_to_screen(app, map, units[i].gx, units[i].gy, &sx, &sy);
        app->cam_x = (float)app->win_w * 0.5f - sx;
        app->cam_y = (float)app->win_h * 0.5f - sy;
        return true;
    }
    return false;
}

static void focus_camera_on_grid(App *app, const GameMap *map, const Plugin *plugin,
                                 float gx, float gy) {
    if (!app || !map) return;
    float sx = 0.0f, sy = 0.0f;
    map_grid_to_screen(app, map, gx, gy, &sx, &sy);
    SDL_Rect viewport = { 0, 0, app->win_w, app->win_h };
    if (plugin && plugin->ui) {
        viewport.x = plugin->ui->world_viewport.x * app->win_w / plugin->ui->logical_width;
        viewport.y = plugin->ui->world_viewport.y * app->win_h / plugin->ui->logical_height;
        viewport.w = plugin->ui->world_viewport.w * app->win_w / plugin->ui->logical_width;
        viewport.h = plugin->ui->world_viewport.h * app->win_h / plugin->ui->logical_height;
    }
    app->cam_x = (float)(viewport.x + viewport.w / 2) - sx;
    app->cam_y = (float)(viewport.y + viewport.h / 2) - sy;
}

static bool focus_camera_on_map_start(App *app, const GameMap *map, const Plugin *plugin) {
    if (!app || !map || !map->has_camera) return false;
    focus_camera_on_grid(app, map, plugin, map->camera_gx, map->camera_gy);
    return true;
}

typedef struct {
    SDL_Rect outer;
    SDL_Rect header;
    SDL_Rect status;
    SDL_Rect commands;
    SDL_Rect resources;
    SDL_Rect minimap;
    SDL_Rect message;
    SDL_Rect build;
    SDL_Rect tabs[3];
    SDL_Rect buttons[8];
} DarkColonyUiLayout;

typedef struct {
    int id;
    int frame;
    char label[40];
} DarkColonySidebarCommand;

typedef struct {
    DarkColonySidebarCommand commands[6];
    int command_count;
} DarkColonySidebar;

typedef struct {
    int ui_id;
    const char *label;
    int cost;
    int icon_frame;
    int product_type;
    int producer_type_id;
    int prerequisites[3];
    int prerequisite_count;
} DarkColonyProductButton;

enum {
    DC_CLIENT_PRODUCT_UNIT = 2,
    DC_CLIENT_MT_TROOPER = 1,
    DC_CLIENT_MT_REAPER = 4,
    DC_CLIENT_MT_THUNDERBOLT = 5,
    DC_CLIENT_MT_CYBORG = 6,
    DC_CLIENT_MT_SCOUT = 7,
    DC_CLIENT_MT_EXPLOITER = 3,
    DC_CLIENT_MT_EXCOPOD = 1000,
    DC_CLIENT_MT_BRRKPOD = 1001,
    DC_CLIENT_MT_ROBOPOD = 1002,
    DC_CLIENT_MT_ROBOPOD2 = 1003,
    DC_CLIENT_MT_SCNCPOD = 1004,
    DC_CLIENT_MT_SCNCPOD2 = 1005,
    DC_CLIENT_MT_RSCHPOD = 1006,
    DC_CLIENT_PRODUCTION_BUILD_GROUP = 6,
    DC_CLIENT_TRSCBUILD_FIRST_FRAME = 12,
};

static const DarkColonyProductButton DARK_COLONY_PRODUCTS[] = {
    {  80, "Barracks",  1000, 20, 17, DC_CLIENT_MT_EXCOPOD, { 0 }, 1 },
    {  81, "Sci-Pod",   2000, 21, 20, DC_CLIENT_MT_EXCOPOD, { 0 }, 1 },
    {  82, "Robo-Ftr",  2000, 22, 18, DC_CLIENT_MT_EXCOPOD, { 2, 1 }, 2 },
    {  83, "Rsch-Bay",  3000, 23, 22, DC_CLIENT_MT_EXCOPOD, { 4 }, 1 },
    {  85, "Sci-Pod+",  2000, 26, 21, DC_CLIENT_MT_EXCOPOD, { 2 }, 1 },
    {  86, "Robo-Ftr+", 2000, 30, 19, DC_CLIENT_MT_EXCOPOD, { 3, 2 }, 2 },
    {  87, "Exploiter", 1500,  8,  6, DC_CLIENT_MT_EXCOPOD, { 0 }, 1 },
    {  89, "Trooper",    350,  6,  0, DC_CLIENT_MT_BRRKPOD, { 1 }, 1 },
    {  90, "Sentinel",   450,  5, 43, DC_CLIENT_MT_BRRKPOD, { 1, 2 }, 2 },
    {  94, "S.A.R.G.E", 1500, 12,  4, DC_CLIENT_MT_BRRKPOD, { 1, 6 }, 2 },
    {  92, "Osprey IV",  600,  9,  5, DC_CLIENT_MT_ROBOPOD, { 0, 3, 4 }, 3 },
    {  91, "Reaper",     600, 11,  2, DC_CLIENT_MT_ROBOPOD, { 3, 2 }, 2 },
    {  88, "Firestorm",  900, 10,  1, DC_CLIENT_MT_ROBOPOD2, { 5 }, 1 },
    {  93, "Barrager",  1000,  7,  3, DC_CLIENT_MT_ROBOPOD2, { 5, 4 }, 2 },
    { 135, "Medi-craft", 900, 29, 49, DC_CLIENT_MT_ROBOPOD, { 4, 3, 6 }, 3 },
};

static uint16_t dc_unit_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 0: return DC_CLIENT_MT_TROOPER;
    case 2: return DC_CLIENT_MT_REAPER;
    case 3: return DC_CLIENT_MT_THUNDERBOLT;
    case 4: return DC_CLIENT_MT_CYBORG;
    case 5: return DC_CLIENT_MT_SCOUT;
    case 6: return DC_CLIENT_MT_EXPLOITER;
    default: return 0;
    }
}

static int dc_product_training_time_ms(const DarkColonyProductButton *product) {
    if (!product) return 0;
    int ms = product->cost * 10;
    if (ms < 1000) ms = 1000;
    return ms;
}

static bool is_dark_colony_plugin(const Plugin *plugin) {
    return plugin && plugin->id && strcmp(plugin->id, "dark-colony") == 0;
}

static void dark_colony_sidebar_defaults(DarkColonySidebar *sidebar) {
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

static SDL_Rect dark_colony_ui_rect(const App *app, int x, int y, int w, int h) {
    int win_w = app && app->win_w > 0 ? app->win_w : 640;
    int win_h = app && app->win_h > 0 ? app->win_h : 480;
    SDL_Rect r = {
        x >= 516 ? win_w - (640 - x) : x,
        y >= 455 ? win_h - (480 - y) : y,
        w,
        h,
    };
    if (r.w < 1 && w > 0) r.w = 1;
    if (r.h < 1 && h > 0) r.h = 1;
    return r;
}

static int gif_read_code(const uint8_t *data, size_t size, size_t *bit_pos, int code_size) {
    int code = 0;
    for (int bit = 0; bit < code_size; ++bit) {
        size_t byte_pos = (*bit_pos) >> 3;
        if (byte_pos >= size) return -1;
        if (data[byte_pos] & (1u << ((*bit_pos) & 7))) code |= 1 << bit;
        (*bit_pos)++;
    }
    return code;
}

static bool gif_decode_lzw(const uint8_t *data, size_t size, int min_code_size,
                           uint8_t *out, size_t out_size) {
    if (!data || !out || min_code_size < 2 || min_code_size > 8) return false;
    enum { GIF_MAX_CODES = 4096 };
    int prefix[GIF_MAX_CODES];
    uint8_t suffix[GIF_MAX_CODES];
    uint8_t stack[GIF_MAX_CODES];
    int clear_code = 1 << min_code_size;
    int end_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = end_code + 1;
    int old_code = -1;
    uint8_t first_char = 0;
    size_t bit_pos = 0, out_pos = 0;

    for (int i = 0; i < GIF_MAX_CODES; ++i) {
        prefix[i] = -1;
        suffix[i] = (uint8_t)i;
    }

    while (out_pos < out_size) {
        int code = gif_read_code(data, size, &bit_pos, code_size);
        if (code < 0) return false;
        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = end_code + 1;
            old_code = -1;
            continue;
        }
        if (code == end_code) break;

        int stack_len = 0;
        int in_code = code;
        if (old_code >= 0 && code == next_code) {
            in_code = old_code;
            stack[stack_len++] = first_char;
        } else if (code > next_code) {
            return false;
        }

        int walk = in_code;
        while (walk >= clear_code) {
            if (walk < 0 || walk >= next_code || stack_len >= GIF_MAX_CODES) return false;
            stack[stack_len++] = suffix[walk];
            walk = prefix[walk];
        }
        if (walk < 0 || walk >= clear_code || stack_len >= GIF_MAX_CODES) return false;
        first_char = (uint8_t)walk;
        stack[stack_len++] = first_char;

        while (stack_len > 0 && out_pos < out_size) out[out_pos++] = stack[--stack_len];

        if (old_code >= 0 && next_code < GIF_MAX_CODES) {
            prefix[next_code] = old_code;
            suffix[next_code] = first_char;
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12) code_size++;
        }
        old_code = code;
    }
    return out_pos == out_size;
}

static bool load_gif_texture(SDL_Renderer *renderer, const char *path, SpriteSheet *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    const uint8_t *bytes = blob.bytes;
    size_t size = blob.size;
    if (size < 13 || (memcmp(bytes, "GIF87a", 6) != 0 && memcmp(bytes, "GIF89a", 6) != 0)) {
        free_blob(&blob);
        return false;
    }

    int canvas_w = read_u16_le(bytes + 6);
    int canvas_h = read_u16_le(bytes + 8);
    uint8_t packed = bytes[10];
    bool has_global_palette = (packed & 0x80) != 0;
    int global_count = has_global_palette ? (2 << (packed & 0x07)) : 0;
    int bg_index = bytes[11];
    size_t pos = 13;
    uint32_t global_palette[256] = { 0 };
    if (global_count > 0) {
        if (pos + (size_t)global_count * 3 > size) { free_blob(&blob); return false; }
        for (int i = 0; i < global_count; ++i) {
            const uint8_t *p = bytes + pos + (size_t)i * 3;
            global_palette[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                                ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        }
        pos += (size_t)global_count * 3;
    }

    int transparent_index = -1;
    uint32_t *canvas = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(uint32_t));
    if (!canvas) { free_blob(&blob); return false; }
    uint32_t bg = bg_index >= 0 && bg_index < global_count ? global_palette[bg_index] : 0xff000000u;
    for (int i = 0; i < canvas_w * canvas_h; ++i) canvas[i] = bg;

    bool decoded = false;
    while (pos < size && !decoded) {
        uint8_t marker = bytes[pos++];
        if (marker == 0x3b) break;
        if (marker == 0x21) {
            if (pos >= size) break;
            uint8_t label = bytes[pos++];
            while (pos < size) {
                uint8_t block_size = bytes[pos++];
                if (block_size == 0) break;
                if (label == 0xf9 && block_size == 4 && pos + 4 <= size) {
                    if (bytes[pos] & 0x01) transparent_index = bytes[pos + 3];
                }
                pos += block_size;
            }
            continue;
        }
        if (marker != 0x2c || pos + 9 > size) break;

        int left = read_u16_le(bytes + pos + 0);
        int top = read_u16_le(bytes + pos + 2);
        int image_w = read_u16_le(bytes + pos + 4);
        int image_h = read_u16_le(bytes + pos + 6);
        uint8_t image_packed = bytes[pos + 8];
        pos += 9;
        bool interlaced = (image_packed & 0x40) != 0;
        bool has_local_palette = (image_packed & 0x80) != 0;
        int local_count = has_local_palette ? (2 << (image_packed & 0x07)) : 0;
        uint32_t local_palette[256] = { 0 };
        uint32_t *palette = global_palette;
        int palette_count = global_count;
        if (has_local_palette) {
            if (pos + (size_t)local_count * 3 > size) break;
            for (int i = 0; i < local_count; ++i) {
                const uint8_t *p = bytes + pos + (size_t)i * 3;
                local_palette[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                                   ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            }
            pos += (size_t)local_count * 3;
            palette = local_palette;
            palette_count = local_count;
        }
        if (pos >= size) break;
        int min_code_size = bytes[pos++];
        uint8_t *compressed = NULL;
        size_t compressed_size = 0;
        while (pos < size) {
            uint8_t block_size = bytes[pos++];
            if (block_size == 0) break;
            if (pos + block_size > size) { free(compressed); free(canvas); free_blob(&blob); return false; }
            uint8_t *next = realloc(compressed, compressed_size + block_size);
            if (!next) { free(compressed); free(canvas); free_blob(&blob); return false; }
            compressed = next;
            memcpy(compressed + compressed_size, bytes + pos, block_size);
            compressed_size += block_size;
            pos += block_size;
        }

        uint8_t *indices = malloc((size_t)image_w * (size_t)image_h);
        if (!indices) { free(compressed); free(canvas); free_blob(&blob); return false; }
        bool ok = gif_decode_lzw(compressed, compressed_size, min_code_size,
                                 indices, (size_t)image_w * (size_t)image_h);
        free(compressed);
        if (!ok) { free(indices); break; }

        int pass_starts[4] = { 0, 4, 2, 1 };
        int pass_steps[4] = { 8, 8, 4, 2 };
        size_t src = 0;
        for (int pass = 0; pass < (interlaced ? 4 : 1); ++pass) {
            int y_start = interlaced ? pass_starts[pass] : 0;
            int y_step = interlaced ? pass_steps[pass] : 1;
            for (int y = y_start; y < image_h; y += y_step) {
                for (int x = 0; x < image_w && src < (size_t)image_w * (size_t)image_h; ++x, ++src) {
                    int index = indices[src];
                    int dx = left + x, dy = top + y;
                    if (dx < 0 || dy < 0 || dx >= canvas_w || dy >= canvas_h ||
                        index == transparent_index || index >= palette_count) {
                        continue;
                    }
                    canvas[dy * canvas_w + dx] = palette[index];
                }
            }
        }
        free(indices);
        decoded = true;
    }

    if (!decoded) {
        free(canvas);
        free_blob(&blob);
        return false;
    }
    out->texture = rgba_texture(renderer, canvas, canvas_w, canvas_h, false);
    free(canvas);
    free_blob(&blob);
    if (!out->texture) return false;
    out->frames = calloc(1, sizeof(SDL_Rect));
    if (!out->frames) { destroy_sprite(out); return false; }
    out->frames[0] = (SDL_Rect){ 0, 0, canvas_w, canvas_h };
    out->frame_count = 1;
    out->frame_w = canvas_w;
    out->frame_h = canvas_h;
    return true;
}

static DarkColonySidebarCommand *dark_colony_sidebar_command(DarkColonySidebar *sidebar, int id) {
    if (!sidebar) return NULL;
    for (int i = 0; i < sidebar->command_count; ++i)
        if (sidebar->commands[i].id == id) return &sidebar->commands[i];
    return NULL;
}

static void dark_colony_sidebar_load(DarkColonySidebar *sidebar, const char *data_root) {
    if (!sidebar || !data_root) return;
    char path[1024];
    path_join(path, sizeof(path), data_root, "INTRFACE/MAINE");
    Blob blob;
    if (!load_blob(path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        return;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    free_blob(&blob);

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
                DarkColonySidebarCommand *cmd = dark_colony_sidebar_command(sidebar, id);
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
                    DarkColonySidebarCommand *cmd = dark_colony_sidebar_command(sidebar, id);
                    if (cmd) cmd->frame = frame;
                }
            }
        }
        line = next;
    }
    free(text);
}

static int dark_colony_ui_width(const App *app) {
    (void)app;
    return 124;
}

static int world_viewport_width(const App *app, const Plugin *plugin) {
    if (!app) return 0;
    if (plugin && plugin->ui && plugin->ui->world_viewport.w > 0)
        return plugin->ui->world_viewport.w * app->win_w / plugin->ui->logical_width;
    int w = app->win_w;
    if (is_dark_colony_plugin(plugin)) w -= dark_colony_ui_width(app);
    return w > 0 ? w : 1;
}

static DarkColonyUiLayout dark_colony_ui_layout(const App *app) {
    DarkColonyUiLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.outer = dark_colony_ui_rect(app, 516, 0, 124, 480);
    layout.minimap = dark_colony_ui_rect(app, 520, 5, 96, 84);
    layout.commands = dark_colony_ui_rect(app, 516, 92, 124, 330);
    layout.status = dark_colony_ui_rect(app, 518, 368, 59, 41);
    layout.resources = dark_colony_ui_rect(app, 607, 422, 28, 51);
    layout.message = dark_colony_ui_rect(app, 48, 456, 462, 16);
    layout.build = dark_colony_ui_rect(app, 516, 422, 86, 27);
    layout.header = dark_colony_ui_rect(app, 516, 0, 124, 92);
    layout.tabs[0] = dark_colony_ui_rect(app, 518, 92, 40, 20);
    layout.tabs[1] = dark_colony_ui_rect(app, 557, 92, 41, 20);
    layout.tabs[2] = dark_colony_ui_rect(app, 598, 92, 40, 20);

    const int button_y[6] = { 112, 153, 194, 235, 276, 317 };
    for (int i = 0; i < 6; ++i) {
        layout.buttons[i] = dark_colony_ui_rect(app, 518, button_y[i], 59, 41);
    }
    return layout;
}

static bool point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static SDL_Rect dark_colony_product_button_rect(const App *app, int index) {
    int col = index / 4;
    int row = index % 4;
    return dark_colony_ui_rect(app, 518 + col * 59, 112 + row * 41, 59, 41);
}

static void dc_ui_set_draw(SDL_Renderer *renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void dc_ui_fill(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

static void dc_ui_stroke(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

static void dc_ui_draw_sprite_fit(SDL_Renderer *renderer, const SpriteSheet *sprite, int frame,
                                  SDL_Rect box, uint32_t render_flags) {
    if (!renderer || !sprite || !sprite->texture || sprite->frame_count <= 0) return;
    if (frame < 0 || frame >= sprite->frame_count) frame = 0;
    SDL_Rect src = sprite->frames[frame];
    if (sprite->frame_bounds && sprite->frame_bounds[frame].w > 0 && sprite->frame_bounds[frame].h > 0) {
        SDL_Rect bounds = sprite->frame_bounds[frame];
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
    SDL_Rect dst = {
        box.x + (box.w - draw_w) / 2,
        box.y + (box.h - draw_h) / 2,
        draw_w,
        draw_h,
    };
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(renderer, sprite->texture, &src, &dst, 0.0, NULL, flip);
}

static void dc_ui_draw_image_part(SDL_Renderer *renderer, const SpriteSheet *image,
                                  SDL_Rect src, SDL_Rect dst) {
    if (!renderer || !image || !image->texture || src.w <= 0 || src.h <= 0 ||
        dst.w <= 0 || dst.h <= 0) {
        return;
    }
    SDL_RenderCopy(renderer, image->texture, &src, &dst);
}

static const Unit *dc_first_selected_unit(const Unit *units, int unit_count) {
    if (!units) return NULL;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) return &units[i];
    }
    return NULL;
}

static int dc_product_row_actor_type(int row_id) {
    switch (row_id) {
    case 0: return DC_CLIENT_MT_EXCOPOD;
    case 1: return DC_CLIENT_MT_BRRKPOD;
    case 2: return DC_CLIENT_MT_SCNCPOD;
    case 3: return DC_CLIENT_MT_ROBOPOD;
    case 4: return DC_CLIENT_MT_SCNCPOD2;
    case 5: return DC_CLIENT_MT_ROBOPOD2;
    case 6: return DC_CLIENT_MT_RSCHPOD;
    case 7: return DC_CLIENT_MT_EXPLOITER;
    default: return 0;
    }
}

static bool dc_player_has_actor_type(const Unit *units, int unit_count, int type_id) {
    if (!units || type_id <= 0) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner == 0 && !units[i].remove && units[i].hp > 0 &&
            units[i].type_id == (uint16_t)type_id) {
            return true;
        }
    }
    return false;
}

static bool dc_product_prerequisites_met(const Unit *units, int unit_count,
                                         const DarkColonyProductButton *product) {
    if (!product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        int actor_type = dc_product_row_actor_type(product->prerequisites[i]);
        if (!dc_player_has_actor_type(units, unit_count, actor_type)) return false;
    }
    return true;
}

static bool dc_selected_unit_is_player_building(const Unit *selected) {
    return selected && selected->owner == 0 && !selected->remove && selected->hp > 0 &&
        selected->type_id >= DC_CLIENT_MT_EXCOPOD;
}

static int dc_products_for_selected_building(const Unit *selected, const Unit *units,
                                             int unit_count,
                                             const DarkColonyProductButton *out[8]) {
    if (!dc_selected_unit_is_player_building(selected) || !out) return 0;
    int count = 0;
    int source_count = (int)(sizeof(DARK_COLONY_PRODUCTS) / sizeof(DARK_COLONY_PRODUCTS[0]));
    for (int i = 0; i < source_count && count < 8; ++i) {
        const DarkColonyProductButton *product = &DARK_COLONY_PRODUCTS[i];
        if (product->producer_type_id != selected->type_id) continue;
        if (!dc_product_prerequisites_met(units, unit_count, product)) continue;
        out[count++] = product;
    }
    return count;
}

static const DarkColonyProductButton *dc_product_by_type(int product_type) {
    int source_count = (int)(sizeof(DARK_COLONY_PRODUCTS) / sizeof(DARK_COLONY_PRODUCTS[0]));
    for (int i = 0; i < source_count; ++i) {
        if (DARK_COLONY_PRODUCTS[i].product_type == product_type)
            return &DARK_COLONY_PRODUCTS[i];
    }
    return NULL;
}

static bool dc_enqueue_unit_product(Unit *producer, const DarkColonyProductButton *product,
                                    uint16_t actor_id) {
    if (!producer || !product || actor_id == 0) return false;
    if (producer->production_queue_count > 0) {
        if (producer->production_actor_id != actor_id ||
            producer->production_product_type != product->product_type ||
            producer->production_product_class != DC_CLIENT_PRODUCT_UNIT ||
            producer->production_queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        producer->production_queue_count++;
        return true;
    }
    producer->production_actor_id = actor_id;
    producer->production_product_class = DC_CLIENT_PRODUCT_UNIT;
    producer->production_product_type = product->product_type;
    producer->production_queue_count = 1;
    producer->production_time_ms = dc_product_training_time_ms(product);
    producer->production_time_left_ms = producer->production_time_ms;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
    return true;
}

static const State *dc_state_at(const GameInfo *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int dc_find_state_by_group_frame(const GameInfo *game_info, int group, int frame) {
    if (!game_info || !game_info->states) return -1;
    for (int i = 0; i < game_info->state_count; ++i) {
        const State *state = &game_info->states[i];
        if (state->misc1 != group || state->facings != 1) continue;
        if (state->facing_frames[0] == frame) return i;
    }
    return -1;
}

static int dc_state_chain_duration_ms(const GameInfo *game_info, int state_id, int group) {
    int tics = 0;
    int guard = 0;
    while (guard++ < (game_info ? game_info->state_count + 1 : 1)) {
        const State *state = dc_state_at(game_info, state_id);
        if (!state || state->misc1 != group) break;
        if (state->tics > 0) tics += state->tics;
        int next = state->nextstate;
        if (next == game_info->null_state || next == state_id) break;
        state_id = next;
    }
    if (tics <= 0) return 0;
    return (int)(tics * FIXED_DT * 1000.0f + 0.5f);
}

static bool dc_product_uses_barracks_release(const Unit *producer,
                                             const DarkColonyProductButton *product,
                                             uint16_t actor_id) {
    return producer && product && producer->type_id == DC_CLIENT_MT_BRRKPOD &&
        product->product_type == 0 && actor_id == DC_CLIENT_MT_TROOPER;
}

static bool dc_start_production_release(const Plugin *plugin, GameMap *map,
                                        VisualEffect *effects, int max_effects,
                                        Unit *producer,
                                        const DarkColonyProductButton *product,
                                        uint16_t actor_id) {
    if (!plugin || !plugin->game_info || !producer || !product) return false;
    if (!dc_product_uses_barracks_release(producer, product, actor_id)) return false;
    const GameInfo *game_info = plugin->game_info;
    int state_id = dc_find_state_by_group_frame(game_info, DC_CLIENT_PRODUCTION_BUILD_GROUP,
                                                DC_CLIENT_TRSCBUILD_FIRST_FRAME);
    int duration_ms = dc_state_chain_duration_ms(game_info, state_id,
                                                 DC_CLIENT_PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    StateContext ctx = {
        .map = map,
        .effects = effects,
        .max_effects = max_effects,
        .game_info = game_info,
    };
    if (!spawn_state_effect(&ctx, state_id, producer->gx, producer->gy, 0)) return false;
    producer->production_release_active = true;
    producer->production_release_time_left_ms = duration_ms;
    producer->production_time_left_ms = 0;
    return true;
}

static void dc_clear_production(Unit *producer) {
    if (!producer) return;
    producer->production_actor_id = 0;
    producer->production_product_class = 0;
    producer->production_product_type = 0;
    producer->production_time_ms = 0;
    producer->production_time_left_ms = 0;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
}

static void dc_advance_production_queue(Unit *producer) {
    if (!producer) return;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
    producer->production_queue_count--;
    if (producer->production_queue_count > 0) {
        producer->production_time_left_ms = producer->production_time_ms;
    } else {
        dc_clear_production(producer);
    }
}

static bool dc_position_available_for_spawn(const GameMap *map, const Unit *units,
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
            if (!map_walkable(map, x, y)) return false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const Unit *other = &units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        float dx = other->gx - gx;
        float dy = other->gy - gy;
        if (dx * dx + dy * dy < min_dist * min_dist) return false;
    }
    return true;
}

static bool dc_position_walkable_for_spawn(const GameMap *map, float gx, float gy,
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
            if (!map_walkable(map, x, y)) return false;
        }
    }
    return true;
}

static bool dc_find_spawn_position_near(const GameMap *map, const Unit *units,
                                        int unit_count, const Unit *producer,
                                        float radius, float *out_gx,
                                        float *out_gy) {
    if (!map || !units || !producer || !out_gx || !out_gy) return false;
    int origin_x = (int)floorf(producer->gx);
    int origin_y = (int)floorf(producer->gy);
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

static bool dc_state_offset_for_facing(const State *state, bool overlay, int facing_code,
                                       int *out_x, int *out_y) {
    if (!state || !out_x || !out_y) return false;
    int facings = overlay ? state->overlay_facings : state->facings;
    if (facings <= 0) return false;
    int best = 0;
    int best_delta = 0x7fffffff;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = overlay ? state->overlay_direction_codes[i] : state->direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
        if (delta == 0) break;
    }
    *out_x = overlay ? state->overlay_offset_x[best] : state->offset_x[best];
    *out_y = overlay ? state->overlay_offset_y[best] : state->offset_y[best];
    return true;
}

static bool dc_barracks_release_spawn_point(const GameInfo *game_info,
                                            const Unit *producer,
                                            const Unit *new_unit,
                                            float *out_gx,
                                            float *out_gy) {
    if (!game_info || !producer || !new_unit || !out_gx || !out_gy) return false;
    const State *stand = dc_state_at(game_info, new_unit->state_id);
    if (!stand) return false;
    int stand_x = 0;
    int stand_y = 0;
    if (!dc_state_offset_for_facing(stand, false, new_unit->facing_code, &stand_x, &stand_y))
        return false;

    int release_state_id = dc_find_state_by_group_frame(game_info,
                                                        DC_CLIENT_PRODUCTION_BUILD_GROUP,
                                                        DC_CLIENT_TRSCBUILD_FIRST_FRAME);
    int release_x = 0;
    int release_y = 0;
    bool saw_release_trooper = false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        const State *state = dc_state_at(game_info, release_state_id);
        if (!state || state->misc1 != DC_CLIENT_PRODUCTION_BUILD_GROUP) break;
        int x = 0;
        int y = 0;
        if (state->sprite == stand->sprite &&
            dc_state_offset_for_facing(state, false, new_unit->facing_code, &x, &y)) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        if (state->overlay_sprite == stand->sprite &&
            dc_state_offset_for_facing(state, true, new_unit->facing_code, &x, &y)) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        int next = state->nextstate;
        if (next == game_info->null_state || next == release_state_id) break;
        release_state_id = next;
    }
    if (!saw_release_trooper) return false;

    *out_gx = producer->gx + (float)(release_x - stand_x) / (float)CELL_W;
    *out_gy = producer->gy - (float)(release_y - stand_y) / (float)CELL_H;
    return true;
}

static void dc_order_barracks_exit_spacing(const GameMap *map, Unit *units, int unit_count,
                                           int spawned_index, const Unit *producer,
                                           float exit_gx, float exit_gy) {
    if (!map || !units || !producer || spawned_index < 0 || spawned_index >= unit_count)
        return;
    bool saved[MAX_UNITS];
    for (int i = 0; i < unit_count; ++i) {
        saved[i] = units[i].selected;
        units[i].selected = false;
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < unit_count; ++i) {
        Unit *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != producer->owner ||
            (unit->traits & RTS_TRAIT_MOBILE) == 0) {
            continue;
        }
        float dx = unit->gx - exit_gx;
        float dy = unit->gy - exit_gy;
        if (i == spawned_index || dx * dx + dy * dy <= crowd_radius_sq) {
            unit->selected = true;
        }
    }

    float dx = exit_gx - producer->gx;
    float dy = exit_gy - producer->gy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {
        dx = 0.0f;
        dy = -1.0f;
        len = 1.0f;
    }
    float goal_gx = exit_gx + dx / len * 1.5f;
    float goal_gy = exit_gy + dy / len * 1.5f;
    issue_move_order_at(map, units, unit_count, goal_gx, goal_gy);

    for (int i = 0; i < unit_count; ++i) {
        units[i].selected = saved[i];
    }
}

static bool dc_spawn_finished_unit_product(const Plugin *plugin, const GameMap *map,
                                           Unit *units, int *unit_count,
                                           int producer_index,
                                           uint16_t actor_id) {
    if (!plugin || !map || !units || !unit_count || producer_index < 0 ||
        producer_index >= *unit_count || *unit_count >= MAX_UNITS || actor_id == 0) {
        return false;
    }
    const ActorType *type = plugin_actor_type_by_id(plugin, actor_id);
    if (!type) return false;

    Unit new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.sprite_id = -1;
    new_unit.attack_target = -1;
    new_unit.harvest_target = -1;
    apply_actor_type_defaults(&new_unit, type);
    apply_mobjinfo_defaults(plugin->game_info, &new_unit);

    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;
    float gx = 0.0f;
    float gy = 0.0f;
    Unit *producer = &units[producer_index];
    const DarkColonyProductButton *product = dc_product_by_type(producer->production_product_type);
    bool use_barracks_release = dc_product_uses_barracks_release(producer, product, actor_id);
    if (use_barracks_release &&
        dc_barracks_release_spawn_point(plugin->game_info, producer, &new_unit, &gx, &gy) &&
        dc_position_walkable_for_spawn(map, gx, gy, radius)) {
        /* The release FIN places the visual handoff; occupied exit cells are cleared below. */
    } else if (!dc_find_spawn_position_near(map, units, *unit_count, producer,
                                            radius, &gx, &gy)) {
        return false;
    }
    new_unit.gx = gx;
    new_unit.gy = gy;
    int spawned_index = *unit_count;
    units[(*unit_count)++] = new_unit;
    if (use_barracks_release)
        dc_order_barracks_exit_spacing(map, units, *unit_count, spawned_index, producer, gx, gy);
    return true;
}

static bool dc_update_production_queues(const Plugin *plugin, GameMap *map,
                                        Unit *units, int *unit_count,
                                        VisualEffect *effects, int max_effects,
                                        float dt) {
    if (!plugin || !map || !units || !unit_count || dt <= 0.0f) return false;
    bool spawned = false;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < *unit_count; ++i) {
        Unit *producer = &units[i];
        if (producer->production_queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            producer->production_queue_count = 0;
            dc_clear_production(producer);
            continue;
        }
        if (producer->production_release_active) {
            producer->production_release_time_left_ms -= elapsed_ms;
            if (producer->production_release_time_left_ms > 0) continue;
            uint16_t actor_id = producer->production_actor_id;
            if (!dc_spawn_finished_unit_product(plugin, map, units, unit_count, i, actor_id)) {
                producer->production_release_time_left_ms = 250;
                continue;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
            continue;
        }
        producer->production_time_left_ms -= elapsed_ms;
        while (producer->production_queue_count > 0 &&
               producer->production_time_left_ms <= 0) {
            uint16_t actor_id = producer->production_actor_id;
            const DarkColonyProductButton *product =
                dc_product_by_type(producer->production_product_type);
            if (product && dc_start_production_release(plugin, map, effects, max_effects,
                                                       producer, product, actor_id)) {
                break;
            }
            if (!dc_spawn_finished_unit_product(plugin, map, units, unit_count, i, actor_id)) {
                producer->production_time_left_ms = 250;
                break;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
        }
    }
    return spawned;
}

static const char *dc_selected_building_label(const Unit *selected) {
    if (!selected) return "";
    switch (selected->type_id) {
    case DC_CLIENT_MT_EXCOPOD: return "Exo-Ctr";
    case DC_CLIENT_MT_BRRKPOD: return "Barracks";
    case DC_CLIENT_MT_ROBOPOD: return "Robo-Ftr";
    case DC_CLIENT_MT_ROBOPOD2: return "Robo-Ftr+";
    case DC_CLIENT_MT_SCNCPOD: return "Sci-Pod";
    case DC_CLIENT_MT_SCNCPOD2: return "Sci-Pod+";
    case DC_CLIENT_MT_RSCHPOD: return "Rsch-Bay";
    default: return "";
    }
}

static int dc_sidebar_command_frame(const DarkColonySidebarCommand *cmd, const Unit *selected) {
    if (!cmd) return 0;
    (void)selected;
    return cmd->frame;
}

static const char *dc_sidebar_command_label(const DarkColonySidebarCommand *cmd,
                                            const Unit *selected) {
    if (!cmd) return "";
    (void)selected;
    if (cmd->id == 37) {
        return "Dig";
    }
    return cmd->label;
}

static void dc_stop_selected_units(Unit *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        units[i].path_len = 0;
        units[i].path_index = 0;
        units[i].attack_target = -1;
        units[i].harvest_target = -1;
        units[i].harvest_timer_ms = 0;
        units[i].move_goal_gx = units[i].gx;
        units[i].move_goal_gy = units[i].gy;
        units[i].move_order_id = 0;
        units[i].move_order_arrived = false;
    }
}

static bool dark_colony_ui_handle_event(const App *app, GameMap *map,
                                        Unit *units, int unit_count,
                                        const SDL_Event *e) {
    if (!app || !map || !e) return false;
    if (e->type != SDL_MOUSEBUTTONDOWN) return false;
    int rx = 0, ry = 0;
    window_to_render_point(app, e->button.x, e->button.y, &rx, &ry);
    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
    if (!point_in_rect(rx, ry, layout.outer)) return false;
    int selected_index = -1;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) {
            selected_index = i;
            break;
        }
    }
    Unit *selected = selected_index >= 0 ? &units[selected_index] : NULL;
    if (dc_selected_unit_is_player_building(selected)) {
        const DarkColonyProductButton *products[8] = { 0 };
        int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
        if (e->button.button == SDL_BUTTON_LEFT) {
            for (int i = 0; i < product_count; ++i) {
                if (!point_in_rect(rx, ry, dark_colony_product_button_rect(app, i))) continue;
                const DarkColonyProductButton *product = products[i];
                uint16_t actor_id = dc_unit_actor_id_for_product_type(product->product_type);
                if (actor_id == 0 || map->player_resources[0] < product->cost) return true;
                if (dc_enqueue_unit_product(selected, product, actor_id)) {
                    map->player_resources[0] -= product->cost;
                }
                return true;
            }
        }
        return true;
    }
    if (e->button.button == SDL_BUTTON_LEFT && point_in_rect(rx, ry, layout.buttons[0])) {
        dc_stop_selected_units(units, unit_count);
    }
    return true;
}

static void dc_ui_draw_minimap(App *app, const GameMap *map, const Unit *units, int unit_count,
                               SDL_Rect rect) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    dc_ui_fill(app->renderer, rect, (SDL_Color){ 4, 8, 9, 255 });
    SDL_Rect clip = { rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6 };
    if (clip.w <= 0 || clip.h <= 0) return;
    for (int py = 0; py < clip.h; ++py) {
        int screen_y = py * map->height / clip.h;
        int gy = map_screen_y_for_cell(map, screen_y);
        for (int px = 0; px < clip.w; ++px) {
            int gx = px * map->width / clip.w;
            uint32_t color = map->cell_colors ? map->cell_colors[map_index(map, gx, gy)] : 0xff202820u;
            uint8_t r = (uint8_t)(color >> 16);
            uint8_t g = (uint8_t)(color >> 8);
            uint8_t b = (uint8_t)color;
            SDL_SetRenderDrawColor(app->renderer, r / 2, g / 2, b / 2, 255);
            SDL_RenderDrawPoint(app->renderer, clip.x + px, clip.y + py);
        }
    }
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const MapResourceVent *vent = &map->resource_vents[i];
        int x = clip.x + vent->gx * clip.w / map->width;
        int y = clip.y + (int)(map_screen_y_for_cell(map, vent->gy) * clip.h / map->height);
        SDL_Rect dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(app->renderer, dot, vent->active ?
                   (SDL_Color){ 89, 226, 184, 255 } : (SDL_Color){ 68, 86, 84, 255 });
    }
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].gx < 0.0f || units[i].gy < 0.0f) continue;
        int x = clip.x + (int)(units[i].gx * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(map_screen_y_for_point(map, units[i].gy) *
                               (float)clip.h / (float)map->height);
        SDL_Rect dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(app->renderer, dot, units[i].owner == 0 ?
                   (SDL_Color){ 218, 214, 135, 255 } : (SDL_Color){ 204, 68, 72, 255 });
    }
    int world_right = app->win_w - dark_colony_ui_width(app);
    Cell tl = screen_to_grid(app, 0, 0);
    Cell br = screen_to_grid(app, world_right, app->win_h);
    int vx = clip.x + tl.x * clip.w / map->width;
    int vy = clip.y + tl.y * clip.h / map->height;
    int vw = (br.x - tl.x) * clip.w / map->width;
    int vh = (br.y - tl.y) * clip.h / map->height;
    if (vw < 3) vw = 3;
    if (vh < 3) vh = 3;
    SDL_Rect view = { vx, vy, vw, vh };
    dc_ui_stroke(app->renderer, view, (SDL_Color){ 164, 236, 203, 220 });
    dc_ui_stroke(app->renderer, rect, (SDL_Color){ 72, 91, 88, 255 });
}

static void dc_ui_draw_text_right(SDL_Renderer *renderer, const BitmapFont *font,
                                  SDL_Rect rect, int y, const char *text,
                                  SDL_Color color) {
    if (!renderer || !font || !text) return;
    int x = rect.x + rect.w - 3 - font_text_width(font, text, 1);
    if (x < rect.x + 2) x = rect.x + 2;
    font_draw_text(renderer, font, x, y, text, color, 1);
}

static void dc_ui_draw_capital(App *app, const BitmapFont *font, SDL_Rect rect,
                               int resources, int active_vents, int vent_count) {
    if (!app || !font) return;
    SDL_Color money = { 41, 217, 230, 255 };
    SDL_Color dim = { 112, 130, 125, 255 };
    char line[32];
    if (resources < 0) resources = 0;
    if (resources >= 1000) {
        snprintf(line, sizeof(line), "%d", resources / 1000);
        dc_ui_draw_text_right(app->renderer, font, rect, rect.y + 5, line, money);
        snprintf(line, sizeof(line), "%03d", resources % 1000);
        dc_ui_draw_text_right(app->renderer, font, rect, rect.y + 20, line, money);
        snprintf(line, sizeof(line), "%d/%d", active_vents, vent_count);
        dc_ui_draw_text_right(app->renderer, font, rect, rect.y + 36, line, dim);
    } else {
        snprintf(line, sizeof(line), "%d", resources);
        dc_ui_draw_text_right(app->renderer, font, rect, rect.y + 8, line, money);
        snprintf(line, sizeof(line), "%d/%d", active_vents, vent_count);
        dc_ui_draw_text_right(app->renderer, font, rect, rect.y + 28, line, dim);
    }
}

static void render_dark_colony_ingame_ui(App *app, const Plugin *plugin, const GameMap *map,
                                         const Unit *units, int unit_count,
                                         const SpriteCache *cache, const BitmapFont *font,
                                         const DarkColonySidebar *sidebar,
                                         const SpriteSheet *background) {
    if (!app || !font || !font->sprite.texture) return;
    (void)plugin;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
    if (background && background->texture) {
        dc_ui_draw_image_part(app->renderer, background,
                              (SDL_Rect){ 516, 0, 124, 480 }, layout.outer);
        dc_ui_draw_image_part(app->renderer, background,
                              (SDL_Rect){ 0, 455, 516, 25 },
                              dark_colony_ui_rect(app, 0, 455, 516, 25));
    } else {
        dc_ui_fill(app->renderer, layout.outer, (SDL_Color){ 2, 2, 2, 255 });
        dc_ui_fill(app->renderer, dark_colony_ui_rect(app, 0, 455, 640, 25),
                   (SDL_Color){ 3, 3, 3, 255 });
        dc_ui_stroke(app->renderer, layout.outer, (SDL_Color){ 178, 178, 178, 255 });
        dc_ui_stroke(app->renderer, dark_colony_ui_rect(app, 0, 455, 640, 18),
                     (SDL_Color){ 164, 164, 164, 255 });
        dc_ui_stroke(app->renderer, layout.minimap, (SDL_Color){ 154, 154, 154, 255 });
        dc_ui_stroke(app->renderer, dark_colony_ui_rect(app, 516, 0, 107, 92),
                     (SDL_Color){ 86, 86, 86, 255 });
        dc_ui_stroke(app->renderer, dark_colony_ui_rect(app, 516, 92, 124, 363),
                     (SDL_Color){ 154, 154, 154, 255 });

        for (int i = 0; i < 3; ++i) {
            dc_ui_fill(app->renderer, layout.tabs[i], (SDL_Color){ 126, 126, 126, 255 });
            dc_ui_stroke(app->renderer, layout.tabs[i], (SDL_Color){ 38, 38, 38, 255 });
            char tab[2] = { (char)('1' + i), '\0' };
            font_draw_text(app->renderer, font,
                               layout.tabs[i].x + layout.tabs[i].w / 2 - font_text_width(font, tab, 1) / 2,
                               layout.tabs[i].y + layout.tabs[i].h / 2 - font->line_h / 2,
                               tab, (SDL_Color){ 24, 24, 24, 255 }, 1);
        }
    }

    SDL_Color dim = { 112, 130, 125, 255 };
    SDL_Color amber = { 231, 194, 94, 255 };
    char line[96];

    const SpriteSheet *buttons = sprite_cache_lookup(cache, "INTRFACE/MAINBUT.SPR");
    SDL_Rect mini = {
        layout.minimap.x + 2,
        layout.minimap.y + 2,
        layout.minimap.w - 4,
        layout.minimap.h - 4,
    };
    dc_ui_draw_minimap(app, map, units, unit_count, mini);

    int active_vents = 0;
    for (int i = 0; i < map->resource_vent_count; ++i)
        if (map->resource_vents[i].active) active_vents++;
    dc_ui_fill(app->renderer, layout.resources, (SDL_Color){ 4, 6, 7, 255 });
    dc_ui_stroke(app->renderer, layout.resources, (SDL_Color){ 142, 142, 142, 255 });
    dc_ui_draw_capital(app, font, layout.resources, map->player_resources[0],
                       active_vents, map->resource_vent_count);

    DarkColonySidebar fallback_sidebar;
    if (!sidebar) {
        dark_colony_sidebar_defaults(&fallback_sidebar);
        sidebar = &fallback_sidebar;
    }
    int hover_button = -1;
    const Unit *selected = dc_first_selected_unit(units, unit_count);
    const DarkColonyProductButton *products[8] = { 0 };
    int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
    bool product_mode = dc_selected_unit_is_player_building(selected);
    int visible_button_count = product_mode ? product_count : sidebar->command_count;
    for (int i = 0; i < visible_button_count; ++i) {
        SDL_Rect button_rect = product_mode ? dark_colony_product_button_rect(app, i) :
            layout.buttons[i];
        if (point_in_rect(app->mouse_x, app->mouse_y, button_rect)) {
            hover_button = i;
            break;
        }
    }
    if (!background || !background->texture) {
        dc_ui_fill(app->renderer, layout.build, (SDL_Color){ 160, 160, 160, 255 });
        dc_ui_stroke(app->renderer, layout.build, (SDL_Color){ 39, 39, 39, 255 });
        font_draw_text(app->renderer, font,
                           layout.build.x + layout.build.w / 2 - font_text_width(font, "BUILD", 5) / 2,
                           layout.build.y + layout.build.h / 2 - font->line_h / 2,
                           "BUILD", (SDL_Color){ 24, 24, 24, 255 }, 1);
    }
    if (hover_button >= 0) {
        if (product_mode && products[hover_button]) {
            snprintf(line, sizeof(line), "%s %d",
                     products[hover_button]->label, products[hover_button]->cost);
            font_draw_text(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line,
                           map->player_resources[0] >= products[hover_button]->cost ?
                           amber : (SDL_Color){ 208, 103, 88, 255 },
                           1);
        } else {
            font_draw_text(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           dc_sidebar_command_label(&sidebar->commands[hover_button], selected),
                           amber, 1);
        }
    } else if (product_mode) {
        if (selected && selected->production_queue_count > 0 &&
            selected->production_time_ms > 0) {
            int done = selected->production_time_ms - selected->production_time_left_ms;
            int pct = done * 100 / selected->production_time_ms;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(line, sizeof(line), "Training x%d %d%%",
                     selected->production_queue_count, pct);
        } else {
            snprintf(line, sizeof(line), "%s", dc_selected_building_label(selected));
        }
        if (line[0] != '\0') {
            font_draw_text(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line, dim, 1);
        }
    }
    int button_slots = product_mode ? 8 : 6;
    for (int i = 0; i < button_slots; ++i) {
        SDL_Rect button_rect = product_mode ? dark_colony_product_button_rect(app, i) :
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
            if (product_mode && products[i] && map->player_resources[0] < products[i]->cost) {
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

static void render_hud_messages(App *app, const HudText *hud, const BitmapFont *font) {
    if (!app || !hud || !font || !font->sprite.texture || hud->count <= 0) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
    dc_ui_fill(app->renderer, layout.message, (SDL_Color){ 3, 5, 5, 255 });
    dc_ui_stroke(app->renderer, layout.message, (SDL_Color){ 142, 142, 142, 255 });
    font_draw_text_wrapped(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                               layout.message.w - 8, hud->messages[hud->count - 1].text,
                               (SDL_Color){ 41, 217, 230, 255 }, 1);
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

static void load_plugin_by_id(const char *game_id) {
    char lib_path[1024];
    /* Try native extension first, then fall back to alternatives */
    const char *extensions[] = { ".dylib", ".so", NULL };
    for (int i = 0; extensions[i]; ++i) {
        snprintf(lib_path, sizeof(lib_path), "build/libs/%s%s", game_id, extensions[i]);
        if (plugin_load(lib_path)) return;
    }
}

int main(int argc, char **argv) {
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : (screenshot_only ? 3 : 1);
    bool software_renderer = false;
    const char *debug_query = NULL;
    bool debug_animation_grid = false;
    const char *game_id = "dark-reign";
    while (argc > arg_base) {
        if (strcmp(argv[arg_base], "--software") == 0) {
            software_renderer = true;
            arg_base += 1;
        } else if (argc > arg_base + 1 && strcmp(argv[arg_base], "--game") == 0) {
            game_id = argv[arg_base + 1];
            arg_base += 2;
        } else if (strncmp(argv[arg_base], "--game=", 7) == 0) {
            game_id = argv[arg_base] + 7;
            arg_base += 1;
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
        } else if (strncmp(argv[arg_base], "--debug-anim=", 13) == 0) {
            debug_query = argv[arg_base] + 13;
            debug_animation_grid = true;
            arg_base += 1;
        } else if (strncmp(argv[arg_base], "--debug-animations=", 19) == 0) {
            debug_query = argv[arg_base] + 19;
            debug_animation_grid = true;
            arg_base += 1;
        } else if ((strcmp(argv[arg_base], "--debug-anim") == 0 ||
                    strcmp(argv[arg_base], "--debug-animations") == 0) &&
                   argc > arg_base + 1) {
            debug_query = argv[arg_base + 1];
            debug_animation_grid = true;
            arg_base += 2;
        } else {
            break;
        }
    }

    load_plugin_by_id(game_id);
    const Plugin *plugin = find_plugin(game_id);
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

    Renderer renderer;
    App app = { 0 };
    if (plugin->ui) {
        app.win_w = plugin->ui->logical_width;
        app.win_h = plugin->ui->logical_height;
    } else if (is_dark_colony_plugin(plugin)) {
        app.win_w = 640;
        app.win_h = 480;
    } else {
        app.win_w = 1280;
        app.win_h = 800;
    }
    app.show_grid = false;
    app.running = true;
    if (!renderer_create(&renderer, sdl_renderer_backend(), "open-rts - paletted RTS base",
                             app.win_w, app.win_h, check_only || screenshot_only,
                             check_only || screenshot_only || software_renderer)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;
    refresh_app_viewport(&app);

    GameMap map;
    if (!plugin->load_map(map_path, &map)) {
        renderer_destroy(&renderer);
        return 1;
    }

    Tileset tileset;
    SpriteSheet unit_sprite;
    DebugOverlay debug_overlay = { 0 };
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!plugin->load_assets(app.renderer, data_root, &map, sprite_name, &tileset, &unit_sprite)) {
        destroy_map(&map);
        renderer_destroy(&renderer);
        return 1;
    }
    if (debug_query && debug_query[0] != '\0') {
        debug_overlay.active = true;
        debug_overlay.animation_grid = debug_animation_grid;
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
        const ActorType *fallback_type = plugin->actor_type_count > 0 ? &plugin->actor_types[0] : NULL;
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
    VisualEffect effects[MAX_VISUAL_EFFECTS] = { 0 };

    SpriteCache decoration_sprites = { 0 };
    if (plugin->load_runtime_sprites &&
        !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                      &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", plugin->name);
    }

    if (!focus_camera_on_map_start(&app, &map, plugin)) {
        float focus_gx = unit_count > 0 ? units[0].gx : (float)map.width * 0.5f;
        float focus_gy = unit_count > 0 ? units[0].gy : (float)map.height * 0.5f;
        focus_camera_on_grid(&app, &map, plugin, focus_gx, focus_gy);
    }
    clamp_camera_to_map(&app, &map, world_viewport_width(&app, plugin), app.win_h);

    printf("Loaded %s (%dx%d, tileset %s, %d units, %d map decorations, %d resource vents). Controls: left select/drag, right move/harvest, Alt+left spawn enemy, WASD/arrows pan, G grid, B blocked overlay, Ctrl+A select all.\n",
           map_path, map.width, map.height, map.tileset_name, unit_count,
           map.decoration_count, map.resource_vent_count);

    if (debug_overlay.active) {
        if (screenshot_only) {
            renderer_begin_frame(&renderer, (SDL_Color){ 10, 12, 16, 255 });
            if (debug_overlay.animation_grid) {
                debug_animation_grid_render(&app, &unit_sprite, sprite_name,
                                            plugin->game_info, &debug_overlay);
            } else {
                debug_overlay_render(&app, &unit_sprite, sprite_name,
                                     plugin->game_info, &debug_overlay);
            }
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
            debug_font_destroy(&debug_overlay.font);
            destroy_sprite_cache(&decoration_sprites);
            destroy_sprite(&unit_sprite);
            destroy_tileset(&tileset);
            destroy_map(&map);
            renderer_destroy(&renderer);
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
            renderer_begin_frame(&renderer, (SDL_Color){ 10, 12, 16, 255 });
            if (debug_overlay.animation_grid) {
                debug_animation_grid_render(&app, &unit_sprite, sprite_name,
                                            plugin->game_info, &debug_overlay);
            } else {
                debug_overlay_render(&app, &unit_sprite, sprite_name,
                                     plugin->game_info, &debug_overlay);
            }
            renderer_end_frame(&renderer);
            SDL_Delay(16);
        }
        debug_font_destroy(&debug_overlay.font);
        destroy_sprite_cache(&decoration_sprites);
        destroy_sprite(&unit_sprite);
        destroy_tileset(&tileset);
        destroy_map(&map);
        renderer_destroy(&renderer);
        return 0;
    }

    BitmapFont ui_font = { 0 };
    SpriteSheet dark_colony_background = { 0 };
    bool dark_colony_ui = is_dark_colony_plugin(plugin);
    bool ui_font_ready = dark_colony_ui && plugin->load_font &&
                         plugin->load_font(app.renderer, data_root, &ui_font);
    if (dark_colony_ui && !ui_font_ready) {
        fprintf(stderr, "warning: failed to create Dark Colony UI font\n");
    }
    if (dark_colony_ui) {
        char ui_background_path[1024];
        path_join(ui_background_path, sizeof(ui_background_path), data_root, "INTRFACE/INTRFACE.GIF");
        if (!load_gif_texture(app.renderer, ui_background_path, &dark_colony_background)) {
            fprintf(stderr, "warning: failed to load Dark Colony UI background %s\n",
                    ui_background_path);
        }
    }
    DarkColonySidebar dark_colony_sidebar;
    dark_colony_sidebar_defaults(&dark_colony_sidebar);
    if (dark_colony_ui) dark_colony_sidebar_load(&dark_colony_sidebar, data_root);
    GameUi game_ui = { 0 };
    if (plugin->ui && !game_ui_load(&game_ui, app.renderer, data_root, plugin->ui))
        fprintf(stderr, "warning: failed to load %s declarative UI\n", plugin->name);
    HudText hud_text = { 0 };
    void *mission = plugin->load_mission ? plugin->load_mission(map_path) : NULL;

    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            if (mission && plugin->update_mission) {
                int before_count = unit_count;
                plugin->update_mission(mission, &map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                                       plugin->game_info, &hud_text, FIXED_DT);
                if (unit_count != before_count && !map.has_camera)
                    focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                clamp_camera_to_map(&app, &map, world_viewport_width(&app, plugin), app.win_h);
            }
            renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            render_map(&app, &map, &tileset);
            render_world_objects(&app, &map, &tileset, units, unit_count, &unit_sprite,
                                 &decoration_sprites, plugin->game_info, SDL_GetTicks());
            render_visual_effects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                                  &decoration_sprites, plugin->game_info);
            if (dark_colony_ui && ui_font_ready) {
                render_dark_colony_ingame_ui(&app, plugin, &map, units, unit_count,
                                             &decoration_sprites, &ui_font, &dark_colony_sidebar,
                                             &dark_colony_background);
                render_hud_messages(&app, &hud_text, &ui_font);
            }
            game_ui_render(&game_ui, &app, &map, units, unit_count, &decoration_sprites);
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s, %d resource vents.\n",
               tileset.count, unit_sprite.frame_count, sprite_name, map.resource_vent_count);
        if (mission && plugin->destroy_mission) plugin->destroy_mission(mission);
        game_ui_destroy(&game_ui);
        destroy_sprite(&dark_colony_background);
        destroy_font(&ui_font);
        destroy_sprite_cache(&decoration_sprites);
        destroy_sprite(&unit_sprite);
        destroy_tileset(&tileset);
        destroy_map(&map);
        renderer_destroy(&renderer);
        return 0;
    }

    uint64_t prev = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    float accumulator = 0.0f;
    int title_resources = -1;

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
            if (dark_colony_ui && dark_colony_ui_handle_event(&app, &map, units, unit_count, &e)) {
                continue;
            }
            handle_event(&app, &map, units, unit_count, &unit_sprite,
                         &decoration_sprites, plugin->game_info, &e);
        }
        update_camera_from_keyboard(&app, frame_dt);
        clamp_camera_to_map(&app, &map, world_viewport_width(&app, plugin), app.win_h);
        while (accumulator >= FIXED_DT) {
            update_units(&map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                         plugin->game_info, FIXED_DT);
            if (mission && plugin->update_mission) {
                int before_count = unit_count;
                plugin->update_mission(mission, &map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                                       plugin->game_info, &hud_text, FIXED_DT);
                if (unit_count != before_count) {
                    if (!map.has_camera) focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                    clamp_camera_to_map(&app, &map, world_viewport_width(&app, plugin), app.win_h);
                    if (plugin->load_runtime_sprites &&
                        !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                                      &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load scripted runtime sprites\n");
                    }
                }
            }
            int before_production_count = unit_count;
            bool production_spawned = dark_colony_ui &&
                dc_update_production_queues(plugin, &map, units, &unit_count,
                                            effects, MAX_VISUAL_EFFECTS, FIXED_DT);
            if (production_spawned || unit_count != before_production_count) {
                if (plugin->load_runtime_sprites &&
                    !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                                  &decoration_sprites)) {
                    fprintf(stderr, "warning: failed to load produced unit sprite\n");
                }
            }
            update_visual_effects(&map, effects, MAX_VISUAL_EFFECTS,
                                  plugin->game_info, FIXED_DT);
            hud_text_update(&hud_text, FIXED_DT);
            accumulator -= FIXED_DT;
        }
        if (map.player_resources[0] != title_resources) {
            char title[128];
            title_resources = map.player_resources[0];
            snprintf(title, sizeof(title), "open-rts - %s - P-7 %d", plugin->name, title_resources);
            SDL_SetWindowTitle(app.window, title);
        }

        app.ticks_ms = SDL_GetTicks();
        renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        render_map(&app, &map, &tileset);
        render_world_objects(&app, &map, &tileset, units, unit_count, &unit_sprite,
                             &decoration_sprites, plugin->game_info, SDL_GetTicks());
        render_visual_effects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                              &decoration_sprites, plugin->game_info);
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        if (dark_colony_ui && ui_font_ready) {
            render_dark_colony_ingame_ui(&app, plugin, &map, units, unit_count,
                                         &decoration_sprites, &ui_font, &dark_colony_sidebar,
                                         &dark_colony_background);
            render_hud_messages(&app, &hud_text, &ui_font);
        }
        game_ui_render(&game_ui, &app, &map, units, unit_count, &decoration_sprites);
        renderer_end_frame(&renderer);
    }

    if (mission && plugin->destroy_mission) plugin->destroy_mission(mission);
    game_ui_destroy(&game_ui);
    destroy_sprite(&dark_colony_background);
    destroy_font(&ui_font);
    destroy_sprite_cache(&decoration_sprites);
    destroy_sprite(&unit_sprite);
    destroy_tileset(&tileset);
    destroy_map(&map);
    renderer_destroy(&renderer);
    return 0;
}
