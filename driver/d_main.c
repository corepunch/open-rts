#define _DEFAULT_SOURCE
#include "engine.h"
#include "game.h"
#include "renderer.h"
#include "st_stuff.h"

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

    font->texture = I_CreateTexture(renderer, pixels, atlas_w, atlas_h, true);
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
        irect_t src = {
            (idx % font->columns) * font->glyph_w,
            (idx / font->columns) * font->glyph_h,
            font->glyph_w,
            font->glyph_h,
        };
        irect_t dst = { cx, cy, font->glyph_w * scale, font->glyph_h * scale };
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

static int debug_sprite_id_for_name(const gameinfo_t *game_info, const char *sprite_name) {
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

static int debug_count_states_for_sprite(const gameinfo_t *game_info, int sprite_id) {
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

static bool debug_state_matches_sprite(const gameinfo_t *game_info, int state_id, int sprite_id) {
    if (!game_info || !game_info->states || state_id == game_info->null_state ||
        state_id < 0 || state_id >= game_info->state_count) {
        return false;
    }
    return game_info->states[state_id].sprite == sprite_id;
}

static bool debug_add_anim_row(const gameinfo_t *game_info, int sprite_id, const char *name,
                               int start_state, DebugAnimRow *rows, int *row_count, int max_rows) {
    if (!game_info || !game_info->states || !rows || !row_count || *row_count >= max_rows) return false;
    if (!debug_state_matches_sprite(game_info, start_state, sprite_id)) return false;
    if (debug_row_start_exists(rows, *row_count, start_state)) return false;

    const state_t *first = &game_info->states[start_state];
    int group = first->misc1;
    DebugAnimRow row;
    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%s", name);

    int state_id = start_state;
    while (row.state_count < (int)(sizeof(row.state_ids) / sizeof(row.state_ids[0]))) {
        if (!debug_state_matches_sprite(game_info, state_id, sprite_id)) break;
        const state_t *state = &game_info->states[state_id];
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

static int debug_collect_anim_rows(const gameinfo_t *game_info, int sprite_id,
                                   DebugAnimRow *rows, int max_rows) {
    if (!game_info || !game_info->mobjinfo || !rows || max_rows <= 0) return 0;
    int row_count = 0;
    for (int i = 0; i < game_info->mobj_type_count; ++i) {
        const mobjinfo_t *info = &game_info->mobjinfo[i];
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

static int debug_state_facing_slot(const state_t *state, int facing_code) {
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

static void debug_resolve_state_frame(const state_t *state, int facing_code,
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

static const state_t *debug_anim_state_for_time(const gameinfo_t *game_info,
                                                 const DebugAnimRow *row, uint32_t ticks_ms) {
    if (!game_info || !game_info->states || !row || row->state_count <= 0) return NULL;
    int total_ms = 0;
    for (int i = 0; i < row->state_count; ++i) {
        const state_t *state = &game_info->states[row->state_ids[i]];
        int tics = state->tics > 0 ? state->tics : 8;
        total_ms += (tics * 1000) / 30;
    }
    if (total_ms <= 0) return &game_info->states[row->state_ids[0]];
    int cursor = row->loop ? (int)(ticks_ms % (uint32_t)total_ms) : (int)(ticks_ms % (uint32_t)total_ms);
    for (int i = 0; i < row->state_count; ++i) {
        const state_t *state = &game_info->states[row->state_ids[i]];
        int tics = state->tics > 0 ? state->tics : 8;
        int duration = (tics * 1000) / 30;
        if (cursor < duration) return state;
        cursor -= duration;
    }
    return &game_info->states[row->state_ids[row->state_count - 1]];
}

/* ── sprite facing grid diagnostic ──────────────────────────────────────── */

static const char *fg_direction_label(DirectionMode mode, int code) {
    if (mode == RTS_DIRECTION_DARK_REIGN_8) {
        static const char *dr8[] = {"N","NE","E","SE","S","SW","W","NW"};
        return dr8[(code / 2) & 7];
    }
    if (mode == RTS_DIRECTION_DARK_COLONY_8) {
        static const char *dc8[] = {"N","NE","E","SE","S","SW","W","NW"};
        return dc8[code & 7];
    }
    if (mode == RTS_DIRECTION_DARK_COLONY_16) {
        static const char *dc16[] = {
            "N","NNE","NE","ENE","E","ESE","SE","SSE",
            "S","SSW","SW","WSW","W","WNW","NW","NNW"
        };
        return dc16[code & 15];
    }
    /* COMPASS_16: atan2f(-dy,dx), sector 0=E, clockwise */
    static const char *c16[] = {"E","NE","N","NW","W","SW","S","SE"};
    return c16[(code / 2) & 7];
}

static const spritesequence_t *fg_seq_find(const spritesheet_t *s, const char *name) {
    if (!s || !name) return NULL;
    for (int i = 0; i < s->sequence_count; ++i)
        if (strcmp(s->sequences[i].name, name) == 0) return &s->sequences[i];
    return NULL;
}

static irect_t fg_frame_rect(const spritesheet_t *s, int frame) {
    if (s && s->frames && frame >= 0 && frame < s->frame_count && s->frames[frame].w > 0)
        return s->frames[frame];
    return (irect_t){0, 0, s ? s->frame_w : 64, s ? s->frame_h : 64};
}

/* Render a grid: rows = sequences (stand/run/…), columns = facing directions.
   Each cell shows the first animation frame for that facing with the compass label
   derived from the sequence's direction_codes[].  Use --show-facings to invoke. */
static void render_sprite_facing_grid(SDL_Renderer *sdl, const spritesheet_t *sprite,
                                      DirectionMode dir_mode, DebugFont *font) {
    if (!sdl || !sprite || !font) return;

    static const char *want[] = {"stand", "run", "walk", "idle", "shoot"};
    const spritesequence_t *seqs[5];
    const char *seq_labels[5];
    int nseqs = 0;
    for (int w = 0; w < 5 && nseqs < 5; ++w) {
        const spritesequence_t *seq = fg_seq_find(sprite, want[w]);
        if (!seq || seq->facings <= 0 || seq->length <= 0) continue;
        bool dup = false;
        for (int j = 0; j < nseqs; ++j) if (seqs[j] == seq) { dup = true; break; }
        if (!dup) { seqs[nseqs] = seq; seq_labels[nseqs] = want[w]; nseqs++; }
    }
    if (nseqs == 0) return;

    int max_facings = 0;
    for (int s = 0; s < nseqs; ++s)
        if (seqs[s]->facings > max_facings) max_facings = seqs[s]->facings;

    int fw = sprite->frame_w > 0 ? sprite->frame_w : 64;
    int fh = sprite->frame_h > 0 ? sprite->frame_h : 64;
    int label_h = 10;
    int row_lbl_w = 40;
    int pad = 4;
    int cell_w = fw + pad;
    int cell_h = fh + label_h + pad;
    int header_h = label_h + pad * 2;

    SDL_SetRenderDrawColor(sdl, 20, 20, 35, 255);
    SDL_RenderClear(sdl);

    /* Column headers from the first sequence's direction_codes */
    const spritesequence_t *ref = seqs[0];
    for (int col = 0; col < ref->facings; ++col) {
        int x = row_lbl_w + col * cell_w;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s/%d",
                 fg_direction_label(dir_mode, ref->direction_codes[col]),
                 ref->direction_codes[col]);
        SDL_Color hcol = {200, 230, 255, 255};
        debug_font_draw_text(sdl, font, x, pad, buf, hcol, 1);
    }

    for (int row = 0; row < nseqs; ++row) {
        const spritesequence_t *seq = seqs[row];
        int row_y = header_h + row * cell_h;

        SDL_Color lbl_col = {255, 200, 60, 255};
        debug_font_draw_text(sdl, font, pad, row_y + fh / 2, seq_labels[row], lbl_col, 1);

        for (int col = 0; col < seq->facings; ++col) {
            int frame_idx = seq->frame_starts[col];
            if (frame_idx < 0 || frame_idx >= sprite->frame_count) continue;

            int cx = row_lbl_w + col * cell_w;
            int cy = row_y;

            irect_t src = fg_frame_rect(sprite, frame_idx);
            irect_t dst = {cx, cy, fw, fh};
            SDL_RenderCopy(sdl, sprite->texture, &src, &dst);

            char fbuf[8];
            snprintf(fbuf, sizeof(fbuf), "#%d", frame_idx);
            SDL_Color gcol = {150, 150, 150, 255};
            debug_font_draw_text(sdl, font, cx, cy + fh + 2, fbuf, gcol, 1);
        }
    }
}

static void debug_animation_grid_render(const app_t *app, const spritesheet_t *sprite,
                                        const char *sprite_name, const gameinfo_t *game_info,
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
    int available_w = app->win.w - 32 - label_w - gap * (cols - 1);
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
    int viewport_h = app->win.h - start_y - 18;
    int max_scroll = content_h > viewport_h ? content_h - viewport_h : 0;
    int scroll_y = overlay->scroll_y;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    for (int dir = 0; dir < cols; ++dir) {
        snprintf(line, sizeof(line), "DIR%d", dir);
        int x = start_x + label_w + dir * (cell_w + gap);
        debug_font_draw_text(app->renderer, &overlay->font, x + 4, start_y - 14, line, cyan, 1);
    }

    irect_t viewport = { 10, start_y - 4, app->win.w - 20, viewport_h + 8 };
    irect_t old_clip = { 0, 0, 0, 0 };
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

        const state_t *state = debug_anim_state_for_time(game_info, row, SDL_GetTicks());
        for (int dir = 0; dir < cols; ++dir) {
            int x = start_x + label_w + dir * (cell_w + gap);
            irect_t cell = { x, y, cell_w, cell_h - 6 };
            SDL_SetRenderDrawColor(app->renderer, 18, 21, 27, 255);
            SDL_RenderFillRect(app->renderer, &cell);
            SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
            SDL_RenderDrawRect(app->renderer, &cell);

            int frame = 0;
            uint32_t flags = 0;
            debug_resolve_state_frame(state, dir, &frame, &flags);
            if (frame < 0 || frame >= sprite->frame_count) continue;

            irect_t dst = {
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

    irect_t border = { 10, 10, app->win.w - 20, app->win.h - 20 };
    SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
    SDL_RenderDrawRect(app->renderer, &border);
}

static void debug_overlay_render(const app_t *app, const spritesheet_t *sprite, const char *sprite_name,
                                 const gameinfo_t *game_info, const DebugOverlay *overlay) {
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
        const spritesequence_t *seq = &sprite->sequences[i];
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
            const state_t *state = &game_info->states[i];
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
    int available_w = app->win.w - 32;
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
    irect_t panel = { 10, start_y - 6, app->win.w - 20, app->win.h - start_y - 14 };
    SDL_RenderFillRect(app->renderer, &panel);

    int viewport_y = start_y - 6;
    int viewport_h = app->win.h - start_y - 14;
    int max_scroll = content_h > viewport_h ? content_h - viewport_h : 0;
    int scroll_y = overlay->scroll_y;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    irect_t viewport = { 10, viewport_y, app->win.w - 20, viewport_h };
    irect_t old_clip = { 0, 0, 0, 0 };
    SDL_RenderGetClipRect(app->renderer, &old_clip);
    SDL_RenderSetClipRect(app->renderer, &viewport);

    for (int i = 0; i < sprite->frame_count; ++i) {
        int row = i / cols;
        int col = i % cols;
        int x = start_x + col * cell_w;
        int y = start_y + row * cell_h - scroll_y;
        if (y + cell_h < viewport_y || y > viewport_y + viewport_h) continue;
        irect_t dst = { x, y, frame_w, frame_h };
        SDL_RenderCopy(app->renderer, sprite->texture, &sprite->frames[i], &dst);
        snprintf(line, sizeof(line), "%d", i);
        debug_font_draw_text(app->renderer, &overlay->font, x, y + frame_h + 2, line, white, 1);
    }

    SDL_RenderSetClipRect(app->renderer, &old_clip);

    irect_t border = { 10, 10, app->win.w - 20, app->win.h - 20 };
    SDL_SetRenderDrawColor(app->renderer, 44, 50, 58, 255);
    SDL_RenderDrawRect(app->renderer, &border);
}

static const actortype_t *actor_type_by_id(uint16_t type_id) {
    const actortype_t *types = (const actortype_t *)mobjinfo;
    if (!types) return NULL;
    for (int i = 0; i < num_mobjinfo; ++i) {
        if (types[i].id == type_id) return &types[i];
    }
    return NULL;
}

static const actortype_t *actor_type_for_unit(const mobj_t *unit) {
    const actortype_t *type = actor_type_by_id(unit ? unit->type_id : 0);
    if (type) return type;
    const actortype_t *types = (const actortype_t *)mobjinfo;
    if (!types || !unit) return NULL;
    for (int i = 0; i < num_mobjinfo; ++i) {
        const char *sprite = types[i].sprite_name;
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->sprite_name) == 0) {
            return &types[i];
        }
    }
    return num_mobjinfo > 0 ? &types[0] : NULL;
}

static void apply_actor_type_defaults(mobj_t *unit, const actortype_t *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    unit->harvest_capacity = type->harvest_capacity;
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

static void apply_actor_defaults(mobj_t *units, int count) {
    for (int i = 0; i < count; ++i) {
        apply_actor_type_defaults(&units[i], actor_type_for_unit(&units[i]));
        P_SpawnMobj(gameinfo, &units[i]);
    }
}

static bool spawn_debug_enemy_unit(const level_t *map, const app_t *app,
                                   mobj_t *units, int *unit_count, int sx, int sy) {
    if (!map || !app || !units || !unit_count || *unit_count >= MAXMOBJS) return false;
    const actortype_t *type = actor_type_by_id(g_debug_enemy_type);
    const actortype_t *types = (const actortype_t *)mobjinfo;
    if (!type && num_mobjinfo > 0) type = &types[0];
    if (!type) return false;
    cell_t cell = R_ScreenToMapGrid(app, map, sx, sy);
    if (!L_Contains(map, cell.x, cell.y)) return false;
    mobj_t *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    unit->gx = (float)cell.x + 0.5f;
    unit->gy = (float)cell.y + 0.5f;
    unit->owner = 1;
    unit->facing_code = (gameinfo &&
        gameinfo->direction_mode != RTS_DIRECTION_DARK_REIGN_8) ? 6 : 8;
    apply_actor_type_defaults(unit, type);
    P_SpawnMobj(gameinfo, unit);
    (*unit_count)++;
    return true;
}

static bool focus_camera_on_first_player_unit(app_t *app, const level_t *map,
                                              const mobj_t *units, int unit_count) {
    if (!app || !map || !units) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        float sx = 0.0f, sy = 0.0f;
        R_MapToScreen(app, map, units[i].gx, units[i].gy, &sx, &sy);
        app->cam.x = (float)app->win.w * 0.5f - sx;
        app->cam.y = (float)app->win.h * 0.5f - sy;
        return true;
    }
    return false;
}

static void focus_camera_on_grid(app_t *app, const level_t *map,
                                 float gx, float gy) {
    if (!app || !map) return;
    float sx = 0.0f, sy = 0.0f;
    R_MapToScreen(app, map, gx, gy, &sx, &sy);
    irect_t viewport = { 0, 0, app->win.w, app->win.h };
    if (gameui) {
        viewport.x = gameui->world_viewport.x * app->win.w / gameui->logical_width;
        viewport.y = gameui->world_viewport.y * app->win.h / gameui->logical_height;
        viewport.w = gameui->world_viewport.w * app->win.w / gameui->logical_width;
        viewport.h = gameui->world_viewport.h * app->win.h / gameui->logical_height;
    }
    app->cam.x = (float)(viewport.x + viewport.w / 2) - sx;
    app->cam.y = (float)(viewport.y + viewport.h / 2) - sy;
}

static bool focus_camera_on_map_start(app_t *app, const level_t *map) {
    if (!app || !map || !map->has_camera) return false;
    focus_camera_on_grid(app, map, map->camera_gx, map->camera_gy);
    return true;
}

typedef struct {
    irect_t outer;
    irect_t header;
    irect_t status;
    irect_t commands;
    irect_t resources;
    irect_t minimap;
    irect_t message;
    irect_t build;
    irect_t tabs[3];
    irect_t buttons[8];
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
    bool active;
    bool font_ready;
    bitmapfont_t font;
    spritesheet_t background;
    DarkColonySidebar sidebar;
} sb_state_t;

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

static irect_t dark_colony_ui_rect(const app_t *app, int x, int y, int w, int h) {
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

static bool load_gif_texture(SDL_Renderer *renderer, const char *path, spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    const uint8_t *bytes = blob.bytes;
    size_t size = blob.size;
    if (size < 13 || (memcmp(bytes, "GIF87a", 6) != 0 && memcmp(bytes, "GIF89a", 6) != 0)) {
        W_FreeFile(&blob);
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
        if (pos + (size_t)global_count * 3 > size) { W_FreeFile(&blob); return false; }
        for (int i = 0; i < global_count; ++i) {
            const uint8_t *p = bytes + pos + (size_t)i * 3;
            global_palette[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                                ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        }
        pos += (size_t)global_count * 3;
    }

    int transparent_index = -1;
    uint32_t *canvas = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(uint32_t));
    if (!canvas) { W_FreeFile(&blob); return false; }
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
            if (pos + block_size > size) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
            uint8_t *next = realloc(compressed, compressed_size + block_size);
            if (!next) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
            compressed = next;
            memcpy(compressed + compressed_size, bytes + pos, block_size);
            compressed_size += block_size;
            pos += block_size;
        }

        uint8_t *indices = malloc((size_t)image_w * (size_t)image_h);
        if (!indices) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
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
        W_FreeFile(&blob);
        return false;
    }
    out->texture = I_CreateTexture(renderer, canvas, canvas_w, canvas_h, false);
    free(canvas);
    W_FreeFile(&blob);
    if (!out->texture) return false;
    out->frames = calloc(1, sizeof(irect_t));
    if (!out->frames) { R_FreeSprite(out); return false; }
    out->frames[0] = (irect_t){ 0, 0, canvas_w, canvas_h };
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

static int dark_colony_ui_width(const app_t *app) {
    (void)app;
    return 124;
}

static int world_viewport_width(const app_t *app) {
    if (!app) return 0;
    if (gameui && gameui->world_viewport.w > 0)
        return gameui->world_viewport.w * app->win.w / gameui->logical_width;
    int w = app->win.w;
    if (strcmp(g_game_id, "dark-colony") == 0) w -= dark_colony_ui_width(app);
    return w > 0 ? w : 1;
}

static DarkColonyUiLayout dark_colony_ui_layout(const app_t *app) {
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

static irect_t dark_colony_product_button_rect(const app_t *app, int index) {
    int col = index / 4;
    int row = index % 4;
    return dark_colony_ui_rect(app, 518 + col * 59, 112 + row * 41, 59, 41);
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

static bool dc_player_has_actor_type(const mobj_t *units, int unit_count, int type_id) {
    if (!units || type_id <= 0) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner == 0 && !units[i].remove && units[i].hp > 0 &&
            units[i].type_id == (uint16_t)type_id) {
            return true;
        }
    }
    return false;
}

static bool dc_product_prerequisites_met(const mobj_t *units, int unit_count,
                                         const DarkColonyProductButton *product) {
    if (!product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        int actor_type = dc_product_row_actor_type(product->prerequisites[i]);
        if (!dc_player_has_actor_type(units, unit_count, actor_type)) return false;
    }
    return true;
}

static bool dc_selected_unit_is_player_building(const mobj_t *selected) {
    return selected && selected->owner == 0 && !selected->remove && selected->hp > 0 &&
        selected->type_id >= DC_CLIENT_MT_EXCOPOD;
}

static int dc_products_for_selected_building(const mobj_t *selected, const mobj_t *units,
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

static bool dc_enqueue_unit_product(mobj_t *producer, const DarkColonyProductButton *product,
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

static const state_t *dc_state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int dc_find_state_by_group_frame(const gameinfo_t *game_info, int group, int frame) {
    if (!game_info || !game_info->states) return -1;
    for (int i = 0; i < game_info->state_count; ++i) {
        const state_t *state = &game_info->states[i];
        if (state->misc1 != group || state->facings != 1) continue;
        if (state->facing_frames[0] == frame) return i;
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
                                             const DarkColonyProductButton *product,
                                             uint16_t actor_id) {
    return producer && product && producer->type_id == DC_CLIENT_MT_BRRKPOD &&
        product->product_type == 0 && actor_id == DC_CLIENT_MT_TROOPER;
}

static bool dc_start_production_release(level_t *map,
                                        effect_t *effects, int max_effects,
                                        mobj_t *producer,
                                        const DarkColonyProductButton *product,
                                        uint16_t actor_id) {
    if (!gameinfo || !producer || !product) return false;
    if (!dc_product_uses_barracks_release(producer, product, actor_id)) return false;
    int state_id = dc_find_state_by_group_frame(gameinfo, DC_CLIENT_PRODUCTION_BUILD_GROUP,
                                                DC_CLIENT_TRSCBUILD_FIRST_FRAME);
    int duration_ms = dc_state_chain_duration_ms(gameinfo, state_id,
                                                 DC_CLIENT_PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    statecontext_t ctx = {
        .map = map,
        .effects = effects,
        .max_effects = max_effects,
        .game_info = gameinfo,
    };
    if (!P_SpawnEffect(&ctx, state_id, producer->gx, producer->gy, 0)) return false;
    producer->production_release_active = true;
    producer->production_release_time_left_ms = duration_ms;
    producer->production_time_left_ms = 0;
    return true;
}

static void dc_clear_production(mobj_t *producer) {
    if (!producer) return;
    producer->production_actor_id = 0;
    producer->production_product_class = 0;
    producer->production_product_type = 0;
    producer->production_time_ms = 0;
    producer->production_time_left_ms = 0;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
}

static void dc_advance_production_queue(mobj_t *producer) {
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
        float dx = other->gx - gx;
        float dy = other->gy - gy;
        if (dx * dx + dy * dy < min_dist * min_dist) return false;
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

static bool dc_state_offset_for_facing(const state_t *state, bool overlay, int facing_code,
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

static bool dc_barracks_release_spawn_point(const gameinfo_t *game_info,
                                            const mobj_t *producer,
                                            const mobj_t *new_unit,
                                            float *out_gx,
                                            float *out_gy) {
    if (!game_info || !producer || !new_unit || !out_gx || !out_gy) return false;
    const state_t *stand = dc_state_at(game_info, new_unit->state_id);
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
        const state_t *state = dc_state_at(game_info, release_state_id);
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
    P_MoveOrderAt(map, units, unit_count, goal_gx, goal_gy);

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
    const actortype_t *type = actor_type_by_id(actor_id);
    if (!type) return false;

    mobj_t new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.sprite_id = -1;
    new_unit.attack_target = -1;
    new_unit.harvest_target = -1;
    apply_actor_type_defaults(&new_unit, type);
    P_SpawnMobj(gameinfo, &new_unit);

    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;
    float gx = 0.0f;
    float gy = 0.0f;
    mobj_t *producer = &units[producer_index];
    const DarkColonyProductButton *product = dc_product_by_type(producer->production_product_type);
    bool use_barracks_release = dc_product_uses_barracks_release(producer, product, actor_id);
    if (use_barracks_release &&
        dc_barracks_release_spawn_point(gameinfo, producer, &new_unit, &gx, &gy) &&
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

static bool dc_update_production_queues(level_t *map,
                                        mobj_t *units, int *unit_count,
                                        effect_t *effects, int max_effects,
                                        float dt) {
    if (!map || !units || !unit_count || dt <= 0.0f) return false;
    bool spawned = false;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < *unit_count; ++i) {
        mobj_t *producer = &units[i];
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
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
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
            if (product && dc_start_production_release(map, effects, max_effects,
                                                       producer, product, actor_id)) {
                break;
            }
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
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

static const char *dc_selected_building_label(const mobj_t *selected) {
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

static int dc_sidebar_command_frame(const DarkColonySidebarCommand *cmd, const mobj_t *selected) {
    if (!cmd) return 0;
    (void)selected;
    return cmd->frame;
}

static const char *dc_sidebar_command_label(const DarkColonySidebarCommand *cmd,
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

static bool SB_responder(const app_t *app, level_t *map,
                         mobj_t *units, int unit_count, const SDL_Event *e) {
    if (!app || !map || !e) return false;
    if (e->type != SDL_MOUSEBUTTONDOWN) return false;
    int rx = 0, ry = 0;
    R_WindowToRenderPt(app, e->button.x, e->button.y, &rx, &ry);
    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
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
        const DarkColonyProductButton *products[8] = { 0 };
        int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
        if (e->button.button == SDL_BUTTON_LEFT) {
            for (int i = 0; i < product_count; ++i) {
                if (!irect_contains(dark_colony_product_button_rect(app, i),
                                    (ivec2_t){ rx, ry })) continue;
                const DarkColonyProductButton *product = products[i];
                uint16_t actor_id = dc_unit_actor_id_for_product_type(product->product_type);
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
        int x = clip.x + vent->gx * clip.w / map->width;
        int y = clip.y + (int)(L_ScreenY(map, vent->gy) * clip.h / map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(app->renderer, dot, vent->active ?
                   (SDL_Color){ 89, 226, 184, 255 } : (SDL_Color){ 68, 86, 84, 255 });
    }
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].gx < 0.0f || units[i].gy < 0.0f) continue;
        int x = clip.x + (int)(units[i].gx * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(L_ScreenYF(map, units[i].gy) *
                               (float)clip.h / (float)map->height);
        irect_t dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(app->renderer, dot, units[i].owner == 0 ?
                   (SDL_Color){ 218, 214, 135, 255 } : (SDL_Color){ 204, 68, 72, 255 });
    }
    int world_right = app->win.w - dark_colony_ui_width(app);
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

static void dc_ui_draw_capital(app_t *app, const bitmapfont_t *font, irect_t rect,
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

static void SB_drawer(app_t *app, const level_t *map,
                      const mobj_t *units, int unit_count,
                      const spritecache_t *cache, const bitmapfont_t *font,
                      const DarkColonySidebar *sidebar,
                      const spritesheet_t *background) {
    if (!app || !font || !font->sprite.texture) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
    if (background && background->texture) {
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 516, 0, 124, 480 }, layout.outer);
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 0, 455, 516, 25 },
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

    int active_vents = 0;
    for (int i = 0; i < map->resource_vent_count; ++i)
        if (map->resource_vents[i].active) active_vents++;
    dc_ui_fill(app->renderer, layout.resources, (SDL_Color){ 4, 6, 7, 255 });
    dc_ui_stroke(app->renderer, layout.resources, (SDL_Color){ 142, 142, 142, 255 });
    dc_ui_draw_capital(app, font, layout.resources, map->player_resources[0][0],
                       active_vents, map->resource_vent_count);

    DarkColonySidebar fallback_sidebar;
    if (!sidebar) {
        dark_colony_sidebar_defaults(&fallback_sidebar);
        sidebar = &fallback_sidebar;
    }
    int hover_button = -1;
    const mobj_t *selected = dc_first_selected_unit(units, unit_count);
    const DarkColonyProductButton *products[8] = { 0 };
    int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
    bool product_mode = dc_selected_unit_is_player_building(selected);
    int visible_button_count = product_mode ? product_count : sidebar->command_count;
    for (int i = 0; i < visible_button_count; ++i) {
        irect_t button_rect = product_mode ? dark_colony_product_button_rect(app, i) :
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
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line, dim, 1);
        }
    }
    int button_slots = product_mode ? 8 : 6;
    for (int i = 0; i < button_slots; ++i) {
        irect_t button_rect = product_mode ? dark_colony_product_button_rect(app, i) :
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
    DarkColonyUiLayout layout = dark_colony_ui_layout(app);
    dc_ui_fill(app->renderer, layout.message, (SDL_Color){ 3, 5, 5, 255 });
    dc_ui_stroke(app->renderer, layout.message, (SDL_Color){ 142, 142, 142, 255 });
    HU_DrawTextWrapped(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                               layout.message.w - 8, hud->messages[hud->count - 1].text,
                               (SDL_Color){ 41, 217, 230, 255 }, 1);
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

/* Hexen-style sidebar lifecycle.  Dark Colony needs a richer interactive bar
   than the declarative Doom-style ST module, so buttons and their responder
   remain owned by SB just as inventory controls are owned by Hexen's SB bar. */
static bool SB_Init(sb_state_t *sb, app_t *app, const char *data_root) {
    if (!sb || !app || !data_root) return false;
    memset(sb, 0, sizeof(*sb));
    sb->active = strcmp(g_game_id, "dark-colony") == 0;
    dark_colony_sidebar_defaults(&sb->sidebar);
    if (!sb->active) return true;

    sb->font_ready = HU_LoadFont(app->renderer, data_root, &sb->font);
    if (!sb->font_ready)
        fprintf(stderr, "warning: failed to create Dark Colony UI font\n");
    dark_colony_sidebar_load(&sb->sidebar, data_root);

    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/INTRFACE.GIF");
    if (!load_gif_texture(app->renderer, path, &sb->background))
        fprintf(stderr, "warning: failed to load Dark Colony UI background %s\n", path);
    return sb->font_ready;
}

static bool SB_Responder(sb_state_t *sb, const app_t *app, level_t *map,
                         mobj_t *units, int unit_count, const SDL_Event *event) {
    return sb && sb->active &&
           SB_responder(app, map, units, unit_count, event);
}

static void SB_Ticker(sb_state_t *sb) {
    (void)sb;
}

static void SB_Drawer(sb_state_t *sb, app_t *app, const level_t *map,
                      const mobj_t *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    if (!sb || !sb->active || !sb->font_ready) return;
    SB_drawer(app, map, units, unit_count, sprites, &sb->font,
              &sb->sidebar, &sb->background);
    render_hud_messages(app, hud, &sb->font);
}

static void SB_Shutdown(sb_state_t *sb) {
    if (!sb) return;
    R_FreeSprite(&sb->background);
    HU_FreeFont(&sb->font);
    memset(sb, 0, sizeof(*sb));
}

int main(int argc, char **argv) {
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    bool show_facings_only = argc > 1 && strcmp(argv[1], "--show-facings") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    const char *show_facings_path = show_facings_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : ((screenshot_only || show_facings_only) ? 3 : 1);
    bool software_renderer = false;
    const char *debug_query = NULL;
    bool debug_animation_grid = false;
    while (argc > arg_base) {
        if (strcmp(argv[arg_base], "--software") == 0) {
            software_renderer = true;
            arg_base += 1;
        } else if (argc > arg_base + 1 && strcmp(argv[arg_base], "--game") == 0) {
            /* --game is ignored: the binary IS the game */
            arg_base += 2;
        } else if (strncmp(argv[arg_base], "--game=", 7) == 0) {
            /* --game=xxx is ignored */
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

    const char *data_root = argc > arg_base ? argv[arg_base] : g_game_default_root;
    const char *map_rel_or_abs = argc > arg_base + 1 ? argv[arg_base + 1] : g_game_default_map;
    const char *sprite_name = argc > arg_base + 2 ? argv[arg_base + 2] : g_game_default_sprite;
    char debug_sprite_name[1024];
    if (debug_query && debug_query[0] != '\0') {
        if (strchr(debug_query, '/') || strchr(debug_query, '.')) {
            snprintf(debug_sprite_name, sizeof(debug_sprite_name), "%s", debug_query);
        } else if (strcmp(g_game_id, "dark-colony") == 0) {
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
        M_PathJoin(map_path, sizeof(map_path), data_root, map_rel_or_abs);
    }

    renderer_t renderer;
    app_t app = { 0 };
    if (gameui) {
        app.win.w = gameui->logical_width;
        app.win.h = gameui->logical_height;
    } else if (strcmp(g_game_id, "dark-colony") == 0) {
        app.win.w = 640;
        app.win.h = 480;
    } else {
        app.win.w = 1280;
        app.win.h = 800;
    }
    if (show_facings_only) { app.win.w = 1280; app.win.h = 720; }
    app.show_grid = false;
    app.running = true;
    if (!renderer_create(&renderer, sdl_renderer_backend(), "open-rts - paletted RTS base",
                             app.win.w, app.win.h,
                             check_only || screenshot_only || show_facings_only,
                             check_only || screenshot_only || show_facings_only || software_renderer)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;
    R_RefreshViewport(&app);

    level_t map;
    if (!G_DoLoadLevel(map_path, &map)) {
        renderer_destroy(&renderer);
        return 1;
    }

    tileset_t tileset;
    spritesheet_t unit_sprite;
    DebugOverlay debug_overlay = { 0 };
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!W_LoadAssets(app.renderer, data_root, &map, sprite_name, &tileset, &unit_sprite)) {
        P_FreeLevel(&map);
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
    app.cell.w = g_cell_w > 0 ? g_cell_w : (tileset.tile_w > 0 ? tileset.tile_w : CELL_W);
    app.cell.h = g_cell_h > 0 ? g_cell_h : (tileset.tile_h > 0 ? tileset.tile_h : CELL_H);

    mobj_t units[MAXMOBJS] = { 0 };
    int unit_count = P_LoadThings(map_path, (mobj_t *)units, MAXMOBJS);
    if (unit_count <= 0) {
        unit_count = 6;
        int cx = map.width / 2;
        int cy = map.height / 2;
        const actortype_t *fallback_type = num_mobjinfo > 0 ? (const actortype_t *)mobjinfo : NULL;
        for (int i = 0; i < unit_count; ++i) {
            units[i].gx = (float)(cx + i % 3) + 0.5f;
            units[i].gy = (float)(cy + i / 3) + 0.5f;
            units[i].owner = 0;
            units[i].selected = i == 0;
            if (fallback_type) {
                apply_actor_type_defaults(&units[i], fallback_type);
            } else {
                units[i].traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE;
                snprintf(units[i].sprite_name, sizeof(units[i].sprite_name), "%s", sprite_name);
            }
        }
    }
    apply_actor_defaults(units, unit_count);
    effect_t effects[MAX_VISUAL_EFFECTS] = { 0 };

    spritecache_t decoration_sprites = { 0 };
    if (!R_InitSprites(app.renderer, data_root, &map, (const mobj_t *)units, unit_count,
                              &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", g_game_name);
    }

    if (!focus_camera_on_map_start(&app, &map)) {
        float focus_gx = unit_count > 0 ? units[0].gx : (float)map.width * 0.5f;
        float focus_gy = unit_count > 0 ? units[0].gy : (float)map.height * 0.5f;
        focus_camera_on_grid(&app, &map, focus_gx, focus_gy);
    }
    R_ClampCamera(&app, &map, world_viewport_width(&app), app.win.h);

    printf("Loaded %s (%dx%d, tileset %s, %d units, %d map decorations, %d resource vents). Controls: left select/drag, right move/harvest, Alt+left spawn enemy, WASD/arrows pan, G grid, B blocked overlay, Ctrl+A select all.\n",
           map_path, map.width, map.height, map.tileset_name, unit_count,
           map.decoration_count, map.resource_vent_count);

    if (show_facings_only) {
        DebugFont font = { 0 };
        bool font_ok = debug_font_init(app.renderer, &font);
        renderer_begin_frame(&renderer, (SDL_Color){ 20, 20, 35, 255 });
        if (font_ok)
            render_sprite_facing_grid(app.renderer, &unit_sprite, map.direction_mode, &font);
        if (show_facings_path) {
            if (renderer_save_screenshot(&renderer, show_facings_path))
                printf("Saved facing grid: %s\n", show_facings_path);
        } else {
            fprintf(stderr, "--show-facings requires an output path\n");
        }
        if (font_ok) debug_font_destroy(&font);
        R_FreeSpriteCache(&decoration_sprites);
        R_FreeSprite(&unit_sprite);
        R_FreeTileset(&tileset);
        P_FreeLevel(&map);
        renderer_destroy(&renderer);
        return 0;
    }

    if (debug_overlay.active) {
        if (screenshot_only) {
            renderer_begin_frame(&renderer, (SDL_Color){ 10, 12, 16, 255 });
            if (debug_overlay.animation_grid) {
                debug_animation_grid_render(&app, &unit_sprite, sprite_name,
                                            gameinfo, &debug_overlay);
            } else {
                debug_overlay_render(&app, &unit_sprite, sprite_name,
                                     gameinfo, &debug_overlay);
            }
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
            debug_font_destroy(&debug_overlay.font);
            R_FreeSpriteCache(&decoration_sprites);
            R_FreeSprite(&unit_sprite);
            R_FreeTileset(&tileset);
            P_FreeLevel(&map);
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
                                            gameinfo, &debug_overlay);
            } else {
                debug_overlay_render(&app, &unit_sprite, sprite_name,
                                     gameinfo, &debug_overlay);
            }
            renderer_end_frame(&renderer);
            SDL_Delay(16);
        }
        debug_font_destroy(&debug_overlay.font);
        R_FreeSpriteCache(&decoration_sprites);
        R_FreeSprite(&unit_sprite);
        R_FreeTileset(&tileset);
        P_FreeLevel(&map);
        renderer_destroy(&renderer);
        return 0;
    }

    sb_state_t sb = { 0 };
    SB_Init(&sb, &app, data_root);
    st_state_t st = { 0 };
    if (gameui && !ST_Init(&st, app.renderer, data_root, gameui))
        fprintf(stderr, "warning: ST_Init failed for %s\n", g_game_name);
    hudtext_t hud_text = { 0 };
    void *mission = G_LoadMission(map_path);

    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            if (mission) {
                int before_count = unit_count;
                G_MissionTicker(mission, &map, (mobj_t *)units, &unit_count,
                                effects, MAX_VISUAL_EFFECTS, &hud_text, FIXED_DT);
                if (unit_count != before_count && !map.has_camera)
                    focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                R_ClampCamera(&app, &map, world_viewport_width(&app), app.win.h);
            }
            renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            R_DrawLevel(&app, &map, &tileset);
            R_RenderPlayerView(&app, &map, &tileset, units, unit_count, &unit_sprite,
                                 &decoration_sprites, gameinfo, SDL_GetTicks());
            R_DrawEffects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                                  &decoration_sprites, gameinfo);
            SB_Drawer(&sb, &app, &map, units, unit_count, &decoration_sprites, &hud_text);
            ST_Drawer(&st, &app, &map, units, unit_count, &decoration_sprites,
                      false, true);
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s, %d resource vents.\n",
               tileset.count, unit_sprite.frame_count, sprite_name, map.resource_vent_count);
        if (mission) G_FreeMission(mission);
        ST_Shutdown(&st);
        SB_Shutdown(&sb);
        R_FreeSpriteCache(&decoration_sprites);
        R_FreeSprite(&unit_sprite);
        R_FreeTileset(&tileset);
        P_FreeLevel(&map);
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
                if (spawn_debug_enemy_unit(&map, &app, units, &unit_count, e.button.x, e.button.y)) {
                    if (!R_InitSprites(app.renderer, data_root, &map,
                                             (const mobj_t *)units, unit_count,
                                             &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load debug enemy sprite\n");
                    }
                }
                continue;
            }
            if (SB_Responder(&sb, &app, &map, units, unit_count, &e) ||
                ST_Responder(&st, &app, &e)) {
                continue;
            }
            G_Responder(&app, &map, units, unit_count, &unit_sprite,
                         &decoration_sprites, gameinfo, &e);
        }
        G_CameraMove(&app, frame_dt);
        R_ClampCamera(&app, &map, world_viewport_width(&app), app.win.h);
        while (accumulator >= FIXED_DT) {
            P_Ticker(&map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                         gameinfo, FIXED_DT);
            if (mission) {
                int before_count = unit_count;
                G_MissionTicker(mission, &map, (mobj_t *)units, &unit_count,
                                effects, MAX_VISUAL_EFFECTS, &hud_text, FIXED_DT);
                if (unit_count != before_count) {
                    if (!map.has_camera) focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                    R_ClampCamera(&app, &map, world_viewport_width(&app), app.win.h);
                    if (!R_InitSprites(app.renderer, data_root, &map,
                                             (const mobj_t *)units, unit_count,
                                             &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load scripted runtime sprites\n");
                    }
                }
            }
            int before_production_count = unit_count;
            bool production_spawned = sb.active &&
                dc_update_production_queues(&map, units, &unit_count,
                                            effects, MAX_VISUAL_EFFECTS, FIXED_DT);
            if (production_spawned || unit_count != before_production_count) {
                if (!R_InitSprites(app.renderer, data_root, &map,
                                         (const mobj_t *)units, unit_count,
                                         &decoration_sprites)) {
                    fprintf(stderr, "warning: failed to load produced unit sprite\n");
                }
            }
            P_UpdateEffects(&map, effects, MAX_VISUAL_EFFECTS,
                                  gameinfo, FIXED_DT);
            HU_Ticker(&hud_text, FIXED_DT);
            ST_Ticker(&st);
            SB_Ticker(&sb);
            accumulator -= FIXED_DT;
        }
        if (map.player_resources[0][0] != title_resources) {
            char title[128];
            title_resources = map.player_resources[0][0];
            snprintf(title, sizeof(title), "open-rts - %s - P-7 %d", g_game_name, title_resources);
            SDL_SetWindowTitle(app.window, title);
        }

        app.ticks_ms = SDL_GetTicks();
        renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        R_DrawLevel(&app, &map, &tileset);
        R_RenderPlayerView(&app, &map, &tileset, units, unit_count, &unit_sprite,
                             &decoration_sprites, gameinfo, SDL_GetTicks());
        R_DrawEffects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                              &decoration_sprites, gameinfo);
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        SB_Drawer(&sb, &app, &map, units, unit_count, &decoration_sprites, &hud_text);
        ST_Drawer(&st, &app, &map, units, unit_count, &decoration_sprites,
                  false, false);
        renderer_end_frame(&renderer);
    }

    if (mission) G_FreeMission(mission);
    ST_Shutdown(&st);
    SB_Shutdown(&sb);
    R_FreeSpriteCache(&decoration_sprites);
    R_FreeSprite(&unit_sprite);
    R_FreeTileset(&tileset);
    P_FreeLevel(&map);
    renderer_destroy(&renderer);
    return 0;
}
