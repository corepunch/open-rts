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

static void debug_append_angles(char *dst, size_t dst_size, const angle_t *values, int count) {
    size_t len = strlen(dst);
    for (int i = 0; i < count && len + 8 < dst_size; ++i) {
        int direction = angle_to_direction(values[i], 32, ANG90, true);
        int written = snprintf(dst + len, dst_size - len, "%s%d",
                               i == 0 ? "" : " ", direction);
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

static const char *fg_direction_label(int code) {
    static const char *const labels[32] = {
        "N", "N", "NNE", "NNE", "NE", "NE", "ENE", "ENE",
        "E", "E", "ESE", "ESE", "SE", "SE", "SSE", "SSE",
        "S", "S", "SSW", "SSW", "SW", "SW", "WSW", "WSW",
        "W", "W", "WNW", "WNW", "NW", "NW", "NNW", "NNW",
    };
    return labels[code & 31];
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
    derived from the sequence's rotation_angles[].  Use --show-facings to invoke. */
static void render_sprite_facing_grid(SDL_Renderer *sdl, const spritesheet_t *sprite,
                                      DebugFont *font) {
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

    /* Column headers from the first sequence's rotation angles. */
    const spritesequence_t *ref = seqs[0];
    for (int col = 0; col < ref->facings; ++col) {
        int x = row_lbl_w + col * cell_w;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s/%d",
                 fg_direction_label(angle_to_direction(ref->rotation_angles[col], 32, ANG90, true)),
                 angle_to_direction(ref->rotation_angles[col], 32, ANG90, true));
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

            int frame = state ? state->frame : 0;
            uint32_t flags = state ? (uint32_t)state->misc2 : 0;
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
        debug_append_angles(dirs, sizeof(dirs), seq->rotation_angles, seq->facings);

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
    int cell_h = frame_h + 28;
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
        irect_t src = sprite->frames[i];
        SDL_Point dis = sprite->frame_displacements ?
            sprite->frame_displacements[i] : (SDL_Point){ 0, 0 };
        irect_t dst = {
            x + dis.x * frame_scale,
            y + dis.y * frame_scale,
            src.w * frame_scale,
            src.h * frame_scale,
        };
        SDL_RenderCopy(app->renderer, sprite->texture, &src, &dst);

        SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 255);
        SDL_RenderDrawLine(app->renderer, x, y, x + 7, y);
        SDL_RenderDrawLine(app->renderer, x, y, x, y + 7);
        if (sprite->frame_bounds) {
            irect_t bounds = sprite->frame_bounds[i];
            irect_t debug_bounds = {
                dst.x + bounds.x * frame_scale,
                dst.y + bounds.y * frame_scale,
                bounds.w * frame_scale,
                bounds.h * frame_scale,
            };
            SDL_SetRenderDrawColor(app->renderer, 232, 93, 86, 255);
            SDL_RenderDrawRect(app->renderer, &debug_bounds);
        }

        snprintf(line, sizeof(line), "F%d %dx%d D%d,%d", i, src.w, src.h, dis.x, dis.y);
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
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->core.sprite_name) == 0) {
            return &types[i];
        }
    }
    return num_mobjinfo > 0 ? &types[0] : NULL;
}

static void apply_actor_type_defaults(mobj_t *unit, const actortype_t *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    unit->harvest.capacity = type->harvest.capacity;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack.range <= 0.0f) unit->attack.range = type->attack.range;
    if (unit->attack.damage <= 0) unit->attack.damage = type->attack.damage;
    if (unit->attack.cooldown_ms <= 0) unit->attack.cooldown_ms = type->attack.cooldown_ms;
    if (unit->attack.anim_ms <= 0) unit->attack.anim_ms = type->attack.anim_ms;
    if (unit->death.anim_ms <= 0) unit->death.anim_ms = type->death.anim_ms;
    if (unit->harvest.state_id <= 0) unit->harvest.state_id = type->harvest.state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    if (unit->attack.target <= 0) unit->attack.target = -1;
    if (unit->harvest.target == 0) unit->harvest.target = -1;
    if (unit->core.sprite_name[0] == '\0' && type->sprite_name) {
        snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", type->sprite_name);
    }
    if (unit->shadow_name[0] == '\0' && type->shadow_name) {
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    }
    if (unit->muzzle_flash_sprite < 0)
        unit->muzzle_flash_sprite = type->muzzle_flash_sprite;
    if (unit->hit_effect_sprite < 0)
        unit->hit_effect_sprite = type->hit_effect_sprite;
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name)
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name),
                 "%s", type->muzzle_flash_name);
    if (unit->hit_effect_name[0] == '\0' && type->hit_effect_name)
        snprintf(unit->hit_effect_name, sizeof(unit->hit_effect_name),
                 "%s", type->hit_effect_name);
    if (!unit->death_effect_action)
        unit->death_effect_action = type->death_effect_action;
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
    unit->core.position = fixedvec3_from_fvec2(
        fvec2_cell_center((ivec2_t){ cell.x, cell.y }), 0);
    unit->owner = 1;
    unit->core.angle = direction_to_angle(12, 32, ANG90, true);
    apply_actor_type_defaults(unit, type);
    P_SpawnMobj(gameinfo, unit);
    (*unit_count)++;
    return true;
}

static bool spawn_debug_harvester_at_vent(level_t *map, mobj_t *units, int *unit_count,
                                          int state_offset) {
    if (!map || !units || !unit_count || *unit_count >= MAXMOBJS || !gameinfo) return false;

    const actortype_t *harvester = NULL;
    const actortype_t *types = (const actortype_t *)mobjinfo;
    for (int i = 0; types && i < num_mobjinfo; ++i) {
        if ((types[i].traits & MF_HARVESTER) != 0) {
            harvester = &types[i];
            break;
        }
    }
    int vent_index = -1;
    float best_distance_sq = 0.0f;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *candidate = &map->resource_vents[i];
        if (!candidate->active) continue;
        float distance_sq = 0.0f;
        if (map->has_camera) {
            distance_sq = fvec2_distance_squared(
                candidate->attachment, map->camera);
        }
        if (vent_index < 0 || distance_sq < best_distance_sq) {
            vent_index = i;
            best_distance_sq = distance_sq;
        }
    }
    if (!harvester || vent_index < 0) return false;

    resourcevent_t *vent = &map->resource_vents[vent_index];
    mobj_t *unit = &units[(*unit_count)++];
    memset(unit, 0, sizeof(*unit));
    unit->core.position = fixedvec3_from_fvec2(vent->attachment, 0);
    unit->core.angle = direction_to_angle(12, 32, ANG90, true);
    unit->owner = 0;
    unit->attack.target = -1;
    unit->harvest.target = vent_index;
    apply_actor_type_defaults(unit, harvester);
    P_SpawnMobj(gameinfo, unit);

    int state_id = harvester->harvest.state_id + state_offset;
    if (state_id < 0 || state_id >= gameinfo->state_count) return false;
    statecontext_t context = { .map = map, .game_info = gameinfo };
    return P_SetMobjState(&context, unit, state_id);
}

static bool focus_camera_on_first_player_unit(app_t *app, const level_t *map,
                                              const mobj_t *units, int unit_count) {
    if (!app || !map || !units) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        float sx = 0.0f, sy = 0.0f;
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        R_MapToScreen(app, map, position.x, position.y, &sx, &sy);
        app->cam.x += (float)app->win.w * 0.5f - sx;
        app->cam.y += (float)app->win.h * 0.5f - sy;
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
    app->cam.x += (float)(viewport.x + viewport.w / 2) - sx;
    app->cam.y += (float)(viewport.y + viewport.h / 2) - sy;
}

static void debug_draw_cross(SDL_Renderer *renderer, int x, int y, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, x - 5, y, x + 5, y);
    SDL_RenderDrawLine(renderer, x, y - 5, x, y + 5);
}

static void debug_draw_map_anchors(const app_t *app, const level_t *map,
                                   const spritecache_t *cache,
                                   const mobj_t *units, int unit_count) {
    if (!app || !app->renderer || !map) return;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        float sx, sy;
        R_MapToScreen(app, map, (float)vent->cell.x, (float)vent->cell.y, &sx, &sy);
        debug_draw_cross(app->renderer, (int)lroundf(sx), (int)lroundf(sy),
                         (SDL_Color){ 232, 93, 86, 255 });
        R_MapToScreen(app, map, vent->attachment.x, vent->attachment.y, &sx, &sy);
        debug_draw_cross(app->renderer, (int)lroundf(sx), (int)lroundf(sy),
                         (SDL_Color){ 255, 224, 92, 255 });
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        float sx, sy;
        fvec2_t anchor = { (float)dec->cell.x, (float)dec->cell.y };
        if (dec->center_anchor) {
            anchor = fvec2_add(anchor, fvec2_scale(
                (fvec2_t){ (float)dec->footprint.w, (float)dec->footprint.h }, 0.5f));
        }
        R_MapToScreen(app, map, anchor.x, anchor.y, &sx, &sy);
        debug_draw_cross(app->renderer, (int)lroundf(sx), (int)lroundf(sy),
                         (SDL_Color){ 98, 224, 161, 255 });
        const spritesheet_t *sprite = cache ? R_CacheLookup(cache, dec->sprite_name) : NULL;
        if (sprite && sprite->frames && sprite->frame_count > 0 && dec->has_sprite_pivot) {
            int frame = dec->frame_index >= 0 ? dec->frame_index :
                (int)((app->ticks_ms / 250u) % (uint32_t)sprite->frame_count);
            irect_t src = sprite->frames[frame];
            irect_t dst = {
                (int)lroundf(sx) - dec->sprite_pivot.x,
                (int)lroundf(sy) - dec->sprite_pivot.y,
                src.w,
                src.h,
            };
            SDL_SetRenderDrawColor(app->renderer, 224, 92, 220, 255);
            SDL_RenderDrawRect(app->renderer, &dst);
        }
    }
    for (int i = 0; units && i < unit_count; ++i) {
        if (units[i].remove || units[i].hp <= 0) continue;
        float sx, sy;
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        R_MapToScreen(app, map, position.x, position.y, &sx, &sy);
        debug_draw_cross(app->renderer, (int)lroundf(sx), (int)lroundf(sy),
                         (SDL_Color){ 88, 160, 255, 255 });
    }
}

static bool focus_camera_on_map_start(app_t *app, const level_t *map) {
    if (!app || !map || !map->has_camera) return false;
    focus_camera_on_grid(app, map, map->camera.x, map->camera.y);
    return true;
}

int main(int argc, char **argv) {
    G_InitGame();
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    bool show_facings_only = argc > 1 && strcmp(argv[1], "--show-facings") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    const char *show_facings_path = show_facings_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : ((screenshot_only || show_facings_only) ? 3 : 1);
    bool software_renderer = false;
    const char *debug_query = NULL;
    bool debug_animation_grid = false;
    bool debug_anchors = false;
    bool debug_grid = false;
    bool debug_terrain_only = false;
    int debug_harvest_state = -1;
    bool debug_camera_override = false;
    fvec2_t debug_camera = { 0.0f, 0.0f };
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
        } else if (strncmp(argv[arg_base], "--camera=", 9) == 0 &&
                   sscanf(argv[arg_base] + 9, "%f,%f", &debug_camera.x, &debug_camera.y) == 2) {
            debug_camera_override = true;
            arg_base += 1;
        } else if (strcmp(argv[arg_base], "--debug-anchors") == 0) {
            debug_anchors = true;
            arg_base += 1;
        } else if (strcmp(argv[arg_base], "--grid") == 0) {
            debug_grid = true;
            arg_base += 1;
        } else if (strcmp(argv[arg_base], "--terrain-only") == 0) {
            debug_terrain_only = true;
            arg_base += 1;
        } else if (strncmp(argv[arg_base], "--debug-harvest-state=", 22) == 0 &&
                   sscanf(argv[arg_base] + 22, "%d", &debug_harvest_state) == 1) {
            arg_base += 1;
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
        } else if (strchr(g_game_default_sprite, '/')) {
            char prefix[256];
            snprintf(prefix, sizeof(prefix), "%s", g_game_default_sprite);
            char *last_slash = strrchr(prefix, '/');
            if (last_slash) *(last_slash + 1) = '\0';
            snprintf(debug_sprite_name, sizeof(debug_sprite_name), "%s%s.SPR", prefix, debug_query);
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
    } else {
        app.win.w = 640;
        app.win.h = 480;
    }
    if (show_facings_only) { app.win.w = 1280; app.win.h = 720; }
    app.show_grid = false;
    app.show_grid = debug_grid;
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
            units[i].core.position = fixedvec3_from_fvec2(fvec2_cell_center(
                (ivec2_t){ cx + i % 3, cy + i / 3 }), 0);
            units[i].owner = 0;
            units[i].selected = i == 0;
            if (fallback_type) {
                apply_actor_type_defaults(&units[i], fallback_type);
            } else {
                units[i].traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE;
                snprintf(units[i].core.sprite_name, sizeof(units[i].core.sprite_name), "%s", sprite_name);
            }
        }
    }
    apply_actor_defaults(units, unit_count);
    int debug_harvester_index = -1;
    if (debug_harvest_state >= 0 &&
        !spawn_debug_harvester_at_vent(&map, units, &unit_count, debug_harvest_state)) {
        fprintf(stderr, "warning: could not create debug harvester state %d\n",
                debug_harvest_state);
    } else if (debug_harvest_state >= 0) {
        debug_harvester_index = unit_count - 1;
    }
    effect_t effects[MAX_VISUAL_EFFECTS] = { 0 };

    spritecache_t decoration_sprites = { 0 };
    if (!R_InitSprites(app.renderer, data_root, &map, (const mobj_t *)units, unit_count,
                              &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", g_game_name);
    }

    if (debug_harvester_index >= 0) {
        fvec2_t position = fixedvec3_xy_to_fvec2(
            units[debug_harvester_index].core.position);
        focus_camera_on_grid(&app, &map,
                             position.x, position.y);
    } else if (!focus_camera_on_map_start(&app, &map)) {
        fvec2_t position = unit_count > 0 ? fixedvec3_xy_to_fvec2(units[0].core.position) :
            (fvec2_t){ (float)map.width * 0.5f, (float)map.height * 0.5f };
        float focus_gx = position.x;
        float focus_gy = position.y;
        focus_camera_on_grid(&app, &map, focus_gx, focus_gy);
    }
    if (debug_camera_override)
        focus_camera_on_grid(&app, &map, debug_camera.x, debug_camera.y);
    R_ClampCamera(&app, &map, G_WorldViewportWidth(&app), app.win.h);

    printf("Loaded %s (%dx%d, tileset %s, %d units, %d map decorations, %d resource vents). Controls: left select/drag, right move/harvest, Alt+left spawn enemy, WASD/arrows pan, G grid, B blocked overlay, Ctrl+A select all, F10 +100 resources.\n",
           map_path, map.width, map.height, map.tileset_name, unit_count,
           map.decoration_count, map.resource_vent_count);

    if (show_facings_only) {
        DebugFont font = { 0 };
        bool font_ok = debug_font_init(app.renderer, &font);
        renderer_begin_frame(&renderer, (SDL_Color){ 20, 20, 35, 255 });
        if (font_ok)
            render_sprite_facing_grid(app.renderer, &unit_sprite, &font);
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

    void *custom_ui = G_InitCustomUI(&app, data_root);
    st_state_t st = { 0 };
    if (gameui && !ST_Init(&st, app.renderer, data_root, gameui))
        fprintf(stderr, "warning: ST_Init failed for %s\n", g_game_name);
    hudtext_t hud_text = { 0 };
    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            if (map.mission) {
                int before_count = unit_count;
                G_MissionTicker(&map, (mobj_t *)units, &unit_count,
                                effects, MAX_VISUAL_EFFECTS, &hud_text, FIXED_DT);
                if (unit_count != before_count && !map.has_camera)
                    focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                R_ClampCamera(&app, &map, G_WorldViewportWidth(&app), app.win.h);
            }
            renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            R_DrawLevel(&app, &map, &tileset);
            R_RenderPlayerView(&app, &map, &tileset, units,
                                 debug_terrain_only ? 0 : unit_count, &unit_sprite,
                                 &decoration_sprites, gameinfo, SDL_GetTicks());
            R_DrawEffects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                                  &decoration_sprites, gameinfo);
            R_DrawGridOverlay(&app, &map);
            if (debug_anchors)
                debug_draw_map_anchors(&app, &map, &decoration_sprites, units, unit_count);
            G_CustomUIDrawer(custom_ui, &app, &map, units, unit_count, &decoration_sprites, &hud_text);
            ST_Drawer(&st, &app, &map, units, unit_count, &decoration_sprites,
                      false, true);
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s, %d resource vents.\n",
               tileset.count, unit_sprite.frame_count, sprite_name, map.resource_vent_count);
        ST_Shutdown(&st);
        G_ShutdownCustomUI(custom_ui);
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
            if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                e.key.keysym.sym == SDLK_F10) {
                map.player_resources[0][0] += 100;
                HU_PushMessage(&hud_text, "CHEAT: +100 RESOURCES", 2000);
                continue;
            }
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
            if (G_CustomUIResponder(custom_ui, &app, &map, units, unit_count, &e) ||
                ST_Responder(&st, &app, &e)) {
                continue;
            }
            G_Responder(&app, &map, units, unit_count, &unit_sprite,
                         &decoration_sprites, gameinfo, &e);
        }
        G_CameraMove(&app, frame_dt);
        R_ClampCamera(&app, &map, G_WorldViewportWidth(&app), app.win.h);
        while (accumulator >= FIXED_DT) {
            P_Ticker(&map, units, &unit_count, effects, MAX_VISUAL_EFFECTS,
                         gameinfo, FIXED_DT);
            if (map.mission) {
                int before_count = unit_count;
                G_MissionTicker(&map, (mobj_t *)units, &unit_count,
                                effects, MAX_VISUAL_EFFECTS, &hud_text, FIXED_DT);
                if (unit_count != before_count) {
                    if (!map.has_camera) focus_camera_on_first_player_unit(&app, &map, units, unit_count);
                    R_ClampCamera(&app, &map, G_WorldViewportWidth(&app), app.win.h);
                    if (!R_InitSprites(app.renderer, data_root, &map,
                                             (const mobj_t *)units, unit_count,
                                             &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load scripted runtime sprites\n");
                    }
                }
            }
            int before_production_count = unit_count;
            bool production_spawned = G_UpdateProduction(custom_ui, &map, units, &unit_count,
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
            G_CustomUITicker(custom_ui);
            accumulator -= FIXED_DT;
        }
        if (map.player_resources[0][0] != title_resources) {
            char title[128];
            title_resources = map.player_resources[0][0];
            snprintf(title, sizeof(title), "open-rts - %s - Resources %d", g_game_name, title_resources);
            SDL_SetWindowTitle(app.window, title);
        }

        app.ticks_ms = SDL_GetTicks();
        renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        R_DrawLevel(&app, &map, &tileset);
        R_RenderPlayerView(&app, &map, &tileset, units, unit_count, &unit_sprite,
                             &decoration_sprites, gameinfo, SDL_GetTicks());
        R_DrawEffects(&app, &map, effects, MAX_VISUAL_EFFECTS,
                              &decoration_sprites, gameinfo);
        R_DrawGridOverlay(&app, &map);
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        G_CustomUIDrawer(custom_ui, &app, &map, units, unit_count, &decoration_sprites, &hud_text);
        ST_Drawer(&st, &app, &map, units, unit_count, &decoration_sprites,
                  false, false);
        renderer_end_frame(&renderer);
    }

    ST_Shutdown(&st);
    G_ShutdownCustomUI(custom_ui);
    R_FreeSpriteCache(&decoration_sprites);
    R_FreeSprite(&unit_sprite);
    R_FreeTileset(&tileset);
    P_FreeLevel(&map);
    renderer_destroy(&renderer);
    return 0;
}
