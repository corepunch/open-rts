/* Generate Dark Colony animate/SPRITE.inc files and the statenum_t enum for
   info.h.  Reads tools/dc_states.txt (authored state policy) and the FIN
   animation files to produce every state_t row used by the game.

   Usage:  dc_info_gen  <ANIMATE-DIR>  <OUT-ANIMATE-DIR>  <OUT-INFO-H>
   Example from the repository root:
       build/dc_info_gen  data/DCOLONY/ANIMATE  games/dark-colony/animate \
                          games/dark-colony/info.h
*/
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── portability ──────────────────────────────────────────────────────────── */

static int mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

/* ── I/O helpers ──────────────────────────────────────────────────────────── */

static void die(const char *ctx, const char *msg) {
    fprintf(stderr, "dc_info_gen: %s: %s\n", ctx, msg);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "dc_info_gen: open %s: %s\n", path, strerror(errno)); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *data = malloc((size_t)n);
    if (!data) die(path, "out of memory");
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return NULL; }
    fclose(f);
    *out_size = (size_t)n;
    return data;
}

static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1] << 8); }
static int16_t  rs16(const uint8_t *p) { return (int16_t)ru16(p); }

/* ── FIN file structures ──────────────────────────────────────────────────── */

/* Maximum sizes for static allocation. */
#define MAX_FIN_LABELS  512
#define MAX_FIN_FRAMES  4096

typedef struct {
    char     name[17];   /* null-terminated label name (max 16 chars in FIN) */
    uint16_t start;
    uint16_t end;
} fin_label_t;

typedef struct {
    char     sprite[9];  /* null-terminated sprite name (8 chars in FIN command) */
    int16_t  cell;       /* SPR cell index from first command */
    uint16_t ticks;
    uint16_t parts;
} fin_frame_t;

typedef struct {
    char        sprite_name[9]; /* derived from FIN filename */
    fin_label_t labels[MAX_FIN_LABELS];
    int         label_count;
    fin_frame_t frames[MAX_FIN_FRAMES];
    int         frame_count;
} fin_t;

/* Parse a FIN file into fin_t.  Returns 1 on success. */
static int parse_fin(const char *path, const char *sprite_name, fin_t *out) {
    size_t size;
    uint8_t *data = read_file(path, &size);
    if (!data) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->sprite_name, sizeof(out->sprite_name), "%s", sprite_name);

    if (size < 8) { free(data); return 0; }
    uint16_t n_frames = ru16(data + 2);
    uint16_t n_labels = ru16(data + 4);
    uint16_t n_deps   = ru16(data + 6);
    if (n_frames > MAX_FIN_FRAMES || n_labels > MAX_FIN_LABELS) { free(data); return 0; }

    size_t label_off = 8 + (size_t)n_deps * 8;
    size_t frame_off = label_off + (size_t)n_labels * 20;
    size_t cmd_off   = frame_off + (size_t)n_frames * 164;
    if (cmd_off > size) { free(data); return 0; }

    out->label_count = (int)n_labels;
    for (int i = 0; i < (int)n_labels; ++i) {
        const uint8_t *p = data + label_off + i * 20;
        /* Copy label name, replacing non-print chars with '_'. */
        for (int j = 0; j < 16 && p[j]; ++j)
            out->labels[i].name[j] = (char)(isprint((unsigned char)p[j]) ? toupper((unsigned char)p[j]) : '_');
        out->labels[i].start = ru16(p + 16);
        out->labels[i].end   = ru16(p + 18);
    }

    /* Pre-compute command offsets for each frame (each frame has variable
       number of 22-byte commands). */
    size_t *cmd_offsets = calloc(n_frames + 1, sizeof(size_t));
    if (!cmd_offsets) die(path, "out of memory");
    size_t used = 0;
    out->frame_count = (int)n_frames;
    for (int i = 0; i < (int)n_frames; ++i) {
        cmd_offsets[i] = used;
        const uint8_t *fhdr = data + frame_off + i * 164;
        out->frames[i].parts = ru16(fhdr);
        out->frames[i].ticks = ru16(fhdr + 2);
        used += (size_t)out->frames[i].parts * 22;
        if (cmd_off + used > size) { /* truncated */ out->frame_count = i; break; }
    }

    /* Read the sprite cell from the first command of each frame. */
    for (int i = 0; i < out->frame_count; ++i) {
        if (out->frames[i].parts == 0) {
            out->frames[i].cell = -1;
            out->frames[i].sprite[0] = 0;
            continue;
        }
        const uint8_t *cmd = data + cmd_off + cmd_offsets[i];
        for (int c = 0; c < 8 && cmd[c]; ++c)
            out->frames[i].sprite[c] = (char)toupper((unsigned char)cmd[c]);
        out->frames[i].cell = rs16(cmd + 8);
    }

    free(cmd_offsets);
    free(data);
    return 1;
}

/* ── Collected FIN database ───────────────────────────────────────────────── */

#define MAX_FINS  256
static fin_t   g_fins[MAX_FINS];
static int     g_fin_count = 0;

static void load_fins(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) die(dir, strerror(errno));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".fin") != 0) continue;
        if (g_fin_count >= MAX_FINS) { fprintf(stderr, "dc_info_gen: too many FINs\n"); break; }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

        /* Derive sprite name from filename without extension, uppercased. */
        char sprite[9] = {0};
        int  len = (int)(dot - e->d_name);
        if (len > 8) len = 8;
        for (int i = 0; i < len; ++i)
            sprite[i] = (char)toupper((unsigned char)e->d_name[i]);

        if (parse_fin(path, sprite, &g_fins[g_fin_count]))
            ++g_fin_count;
    }
    closedir(d);
}

/* ── State definition (from dc_states.txt) ───────────────────────────────── */

#define MAX_STATES      16384
#define MAX_NAME         64

typedef struct {
    char name[MAX_NAME];   /* e.g. "S_TRSC_STND" */
    char sprite[16];       /* e.g. "TRSC" */
    int  frame;            /* SPR cell index */
    int  tics;             /* -1 = infinite */
    char action[64];       /* e.g. "A_Look" */
    char next[MAX_NAME];   /* e.g. "S_TRSC_STND" */
    int  group;
    int  auto_gen;         /* 1 = generated from FIN label (not from dc_states.txt) */
} state_def_t;

static state_def_t g_states[MAX_STATES];
static int         g_state_count = 0;

static state_def_t *add_state(void) {
    if (g_state_count >= MAX_STATES) die("states", "too many states");
    return &g_states[g_state_count++];
}

/* ── dc_states.txt DSL parser ─────────────────────────────────────────────── */

/* Trim leading/trailing whitespace in place. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

/* Split a string by whitespace into up to `max` tokens. Returns token count.
   Modifies s in-place (NUL-terminates tokens). Non-reentrant-safe version. */
static int split(char *s, char **tok, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s && isspace((unsigned char)*s)) ++s;
        if (!*s) break;
        tok[n++] = s;
        while (*s && !isspace((unsigned char)*s)) ++s;
        if (*s) *s++ = 0;
    }
    return n;
}

/* Parse an integer or return -999 on failure. */
static int parse_int(const char *s) {
    if (!s || !*s) return -999;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *end) return -999;
    return (int)v;
}

/* Split a string by commas into up to `max` integer tokens. */
static int split_commas(char *s, int *out, int max) {
    int n = 0;
    char *p = s;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') ++p;
        char *start = p;
        while (*p && *p != ',') ++p;
        char save = *p;
        *p = 0;
        out[n++] = parse_int(trim(start));
        *p = save;
        if (*p) ++p;
    }
    return n;
}

/* Ticks token: either a scalar integer, "-1", or a comma list.
   Writes at most `n` values to `out`. Returns count written. */
static int parse_ticks(const char *s, int *out, int n) {
    char buf[512];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int vals[512], vcount = 0;
    vcount = split_commas(buf, vals, 512);
    if (vcount == 0) { vals[0] = parse_int(s); vcount = 1; }
    for (int i = 0; i < n; ++i)
        out[i] = (vcount == 1) ? vals[0] : vals[i % vcount];
    return vcount;
}

/* Expand a state name template with `{1..N}` to `n` names.
   `n` must equal the range size. Writes each to out[i] (MAX_NAME bytes each). */
static void expand_name(const char *tmpl, int start, int n, char out[][MAX_NAME]) {
    const char *lb = strchr(tmpl, '{');
    if (!lb) {
        for (int i = 0; i < n; ++i) snprintf(out[i], MAX_NAME, "%s", tmpl);
        return;
    }
    const char *rb = strchr(lb, '}');
    if (!rb) { for (int i = 0; i < n; ++i) snprintf(out[i], MAX_NAME, "%s", tmpl); return; }
    int prefix_len = (int)(lb - tmpl);
    for (int i = 0; i < n; ++i)
        snprintf(out[i], MAX_NAME, "%.*s%d%s", prefix_len, tmpl, start + i, rb + 1);
}

/* Parse a frame token: either a scalar integer or "a..b" range.
   Returns the number of frames; writes frame indices to `frames[]` (up to max). */
static int parse_frames(const char *s, int *frames, int max) {
    const char *dots = strstr(s, "..");
    if (dots) {
        char *endp;
        int a = (int)strtol(s, &endp, 10);
        int b = (int)strtol(dots + 2, &endp, 10);
        int n = b - a + 1;
        if (n < 1 || n > max) n = max;
        for (int i = 0; i < n; ++i) frames[i] = a + i;
        return n;
    }
    frames[0] = parse_int(s);
    return 1;
}

/* Columns in dc_states.txt: state  sprite  frame  ticks  action  next  group */
#define COL_STATE  0
#define COL_SPRITE 1
#define COL_FRAME  2
#define COL_TICKS  3
#define COL_ACTION 4
#define COL_NEXT   5
#define COL_GROUP  6
#define NCOLS      7

static void parse_dc_states(const char *path) {
    size_t size;
    uint8_t *raw = read_file(path, &size);
    if (!raw) die(path, "cannot read dc_states.txt");

    char *text = (char *)raw;
    char *line = strtok(text, "\n");

    while (line) {
        char *l = trim(line);
        line = strtok(NULL, "\n");
        if (!*l || *l == ';') continue;

        /* Remove inline comment. */
        char *sc = strchr(l, ';');
        if (sc) *sc = 0;
        l = trim(l);
        if (!*l) continue;

        char *tok[NCOLS];
        int ntok = split(l, tok, NCOLS);
        if (ntok < 7) continue; /* skip malformed lines */

        const char *state_tmpl = tok[COL_STATE];
        const char *sprite     = tok[COL_SPRITE];
        const char *frame_tok  = tok[COL_FRAME];
        const char *ticks_tok  = tok[COL_TICKS];
        const char *action     = tok[COL_ACTION];
        const char *next_tmpl  = tok[COL_NEXT];
        int         group      = parse_int(tok[COL_GROUP]);

        /* Determine expansion count from {start..end} in state name. */
        int  name_start = 0, name_end = 0, expand_n = 1;
        const char *lb = strchr(state_tmpl, '{');
        if (lb) {
            const char *rb = strchr(lb, '}');
            if (rb) {
                const char *dots = strstr(lb, "..");
                if (dots && dots < rb) {
                    name_start = (int)strtol(lb + 1, NULL, 10);
                    name_end   = (int)strtol(dots + 2, NULL, 10);
                    expand_n   = name_end - name_start + 1;
                    if (expand_n < 1) expand_n = 1;
                }
            }
        }

        /* Expand frame range. */
        int frame_vals[MAX_STATES];
        int nframes = parse_frames(frame_tok, frame_vals, MAX_STATES);
        if (nframes < expand_n) {
            /* Scalar: repeat last value. */
            for (int i = nframes; i < expand_n; ++i)
                frame_vals[i] = frame_vals[nframes - 1];
        }

        /* Expand ticks. */
        int ticks_vals[MAX_STATES];
        parse_ticks(ticks_tok, ticks_vals, expand_n);

        /* Expand state names. */
        char (*names)[MAX_NAME] = calloc((size_t)expand_n, MAX_NAME);
        if (!names) die("parse_dc_states", "out of memory");
        expand_name(state_tmpl, name_start, expand_n, names);

        /* Build next-state chain:
           - If next_tmpl references the base pattern (e.g. S_TRSC_RUN1), and
             we expanded a series, intermediate states chain to their successor.
           - If next_tmpl is S_NULL or doesn't contain {}, keep as is. */
        for (int i = 0; i < expand_n; ++i) {
            state_def_t *sd = add_state();
            snprintf(sd->name,   sizeof(sd->name),   "%s", names[i]);
            snprintf(sd->sprite, sizeof(sd->sprite),  "%s", sprite);
            sd->frame = frame_vals[i < nframes ? i : nframes - 1];
            sd->tics  = ticks_vals[i];
            snprintf(sd->action, sizeof(sd->action), "%s", action);
            sd->group    = group;
            sd->auto_gen = 0;

            /* Compute next state. */
            if (expand_n == 1 || i == expand_n - 1) {
                /* Last in series: use next_tmpl verbatim (or expanded with name_start). */
                snprintf(sd->next, sizeof(sd->next), "%s", next_tmpl);
            } else {
                /* Intermediate: chain to the following state in the series. */
                snprintf(sd->next, sizeof(sd->next), "%s", names[i + 1]);
            }
        }
        free(names);
    }
    free(raw);
}

/* ── Auto-generate states from FIN labels (blood, damage, etc.) ───────────── */

/* Determines whether a FIN label name should produce auto-generated states.
   Labels that match blood or building-damage naming conventions are included.
   Labels already covered by dc_states.txt are excluded at call site. */
static int is_autogen_label(const char *name) {
    /* Blood/gore: ends with BLOODx0 (x = a-g) */
    if (strstr(name, "BLOOD")) return 1;
    /* Building damage sequences */
    if (strstr(name, "SCRCH"))  return 1;
    if (strstr(name, "BURN"))   return 1;
    if (strstr(name, "PODDIE")) return 1;
    /* Also allow explicit FIN labels used in mobjinfo death/damage lookups */
    /* (the BIOHIV, WARHIV, etc. labels in ALBU.FIN) */
    if (strstr(name, "HIV"))    return 1;
    return 0;
}

/* Build the state name for an auto-generated state:
   "S_LABELNAME_FRAMEINDEX", e.g. "S_TRSCBLOODA0_313". */
static void autogen_state_name(const char *label, int frame_idx, char *out, size_t out_size) {
    snprintf(out, out_size, "S_%s_%d", label, frame_idx);
}

static void generate_autogen_states(void) {
    for (int fi = 0; fi < g_fin_count; ++fi) {
        fin_t *fin = &g_fins[fi];
        for (int li = 0; li < fin->label_count; ++li) {
            fin_label_t *lbl = &fin->labels[li];
            if (!lbl->name[0]) continue;
            if (!is_autogen_label(lbl->name)) continue;

            /* Check if dc_states.txt already defines a state using this label. */
            /* (We check by label-derived name prefix S_LABELNAME_.) */
            char prefix[MAX_NAME];
            snprintf(prefix, sizeof(prefix), "S_%s_", lbl->name);
            int already = 0;
            for (int si = 0; si < g_state_count; ++si) {
                if (strncmp(g_states[si].name, prefix, strlen(prefix)) == 0) {
                    already = 1; break;
                }
            }
            if (already) continue;

            /* Generate one state per frame in the label range. */
            int start = (int)lbl->start, end = (int)lbl->end;
            if (start > end || end >= fin->frame_count) continue;

            for (int f = start; f <= end; ++f) {
                state_def_t *sd = add_state();
                autogen_state_name(lbl->name, f, sd->name, sizeof(sd->name));
                snprintf(sd->sprite, sizeof(sd->sprite), "%s", fin->sprite_name);
                sd->frame    = (f < fin->frame_count) ? (int)fin->frames[f].cell : 0;
                sd->tics     = 4; /* default blood animation speed */
                sd->group    = 0;
                sd->auto_gen = 1;
                snprintf(sd->action, sizeof(sd->action), "NULL");
                /* Chain to next state in sequence, or S_NULL at end. */
                if (f < end) autogen_state_name(lbl->name, f + 1, sd->next, sizeof(sd->next));
                else         snprintf(sd->next, sizeof(sd->next), "S_NULL");
            }
        }
    }
}

/* ── Group states by sprite for .inc output ───────────────────────────────── */

static void collect_sprites(char sprites[][16], int *count, int max) {
    *count = 0;
    for (int i = 0; i < g_state_count; ++i) {
        const char *sp = g_states[i].sprite;
        int found = 0;
        for (int j = 0; j < *count; ++j)
            if (strcasecmp(sprites[j], sp) == 0) { found = 1; break; }
        if (!found && *count < max) snprintf(sprites[(*count)++], 16, "%s", sp);
    }
}

/* ── Write animate/SPRITE.inc ─────────────────────────────────────────────── */

static void write_inc(const char *animate_dir, const char *sprite) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.inc", animate_dir, sprite);

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "dc_info_gen: open %s: %s\n", path, strerror(errno)); return; }

    fprintf(f, "/* Generated by tools/dc_info_gen.c. Do not edit by hand. */\n");

    for (int i = 0; i < g_state_count; ++i) {
        state_def_t *sd = &g_states[i];
        if (strcasecmp(sd->sprite, sprite) != 0) continue;
        fprintf(f, "    [%s] = { SPR_%s, %d, %d, %s, %s, %d },\n",
                sd->name, sd->sprite, sd->frame, sd->tics,
                sd->action, sd->next, sd->group);
    }
    fclose(f);
}

/* ── Write the statenum_t enum section to info.h ─────────────────────────── */

/* Finds and replaces the statenum_t enum in info.h using marker comments,
   OR appends a new one if no markers are present. */
static void write_info_h_enum(const char *info_h_path) {
    /* Read the existing file. */
    size_t old_size = 0;
    uint8_t *old_data = read_file(info_h_path, &old_size);

    const char *MARKER_BEGIN = "/* BEGIN_GENERATED_STATENUM */";
    const char *MARKER_END   = "/* END_GENERATED_STATENUM */";

    /* Build the generated block. */
    char *block = NULL;
    size_t block_size = 0;
    FILE *mb = open_memstream(&block, &block_size);
    if (!mb) die(info_h_path, "open_memstream failed");

    fprintf(mb, "%s\n", MARKER_BEGIN);
    fprintf(mb, "typedef enum {\n");
    fprintf(mb, "    S_NULL = 0,\n");
    for (int i = 0; i < g_state_count; ++i) {
        fprintf(mb, "    %s,\n", g_states[i].name);
    }
    fprintf(mb, "    NUMSTATES\n");
    fprintf(mb, "} statenum_t;\n");
    fprintf(mb, "%s\n", MARKER_END);
    fclose(mb);

    FILE *out = fopen(info_h_path, "w");
    if (!out) die(info_h_path, strerror(errno));

    if (old_data) {
        const char *old = (const char *)old_data;
        const char *begin_pos = strstr(old, MARKER_BEGIN);
        const char *end_pos   = begin_pos ? strstr(begin_pos, MARKER_END) : NULL;

        if (begin_pos && end_pos) {
            /* Replace between markers. */
            fwrite(old, 1, (size_t)(begin_pos - old), out);
            fwrite(block, 1, block_size, out);
            const char *after = end_pos + strlen(MARKER_END);
            if (*after == '\n') ++after;
            fwrite(after, 1, old_size - (size_t)(after - old), out);
        } else {
            /* No markers: find existing statenum_t and replace it. */
            const char *enum_start = strstr(old, "typedef enum {");
            /* Look for the one that contains S_NULL. */
            while (enum_start) {
                if (strstr(enum_start, "S_NULL") &&
                    strstr(enum_start, "NUMSTATES")) break;
                enum_start = strstr(enum_start + 1, "typedef enum {");
            }
            if (enum_start) {
                const char *enum_end = strstr(enum_start, "} statenum_t;");
                if (enum_end) {
                    fwrite(old, 1, (size_t)(enum_start - old), out);
                    fwrite(block, 1, block_size, out);
                    const char *after = enum_end + strlen("} statenum_t;");
                    if (*after == '\n') ++after;
                    fwrite(after, 1, old_size - (size_t)(after - old), out);
                } else {
                    /* Fall back: append. */
                    fwrite(old, 1, old_size, out);
                    fputc('\n', out);
                    fwrite(block, 1, block_size, out);
                }
            } else {
                fwrite(old, 1, old_size, out);
                fputc('\n', out);
                fwrite(block, 1, block_size, out);
            }
        }
        free(old_data);
    } else {
        /* New file: just write the block. */
        fwrite(block, 1, block_size, out);
    }
    fclose(out);
    free(block);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
            "dc_info_gen: usage: dc_info_gen <FIN-DIR> <OUT-ANIMATE-DIR> <INFO-H>\n"
            "  FIN-DIR         directory containing *.FIN files\n"
            "  OUT-ANIMATE-DIR directory to write SPRITE.inc files into\n"
            "  INFO-H          info.h file to update (statenum_t enum section)\n"
            "  Reads tools/dc_states.txt from the current working directory.\n");
        return 1;
    }

    const char *fin_dir     = argv[1];
    const char *animate_dir = argv[2];
    const char *info_h      = argv[3];

    /* 1. Load all FIN files from fin_dir. */
    load_fins(fin_dir);
    if (g_fin_count == 0) {
        fprintf(stderr, "dc_info_gen: no FIN files found in %s\n", fin_dir);
        return 1;
    }
    fprintf(stderr, "dc_info_gen: loaded %d FIN files\n", g_fin_count);

    /* 2. Parse tools/dc_states.txt (from CWD). */
    parse_dc_states("tools/dc_states.txt");
    fprintf(stderr, "dc_info_gen: %d authored states from dc_states.txt\n", g_state_count);

    /* 3. Generate auto states from FIN blood/damage labels. */
    generate_autogen_states();
    fprintf(stderr, "dc_info_gen: %d total states (incl. autogen)\n", g_state_count);

    /* 4. Write animate/SPRITE.inc files, one per sprite. */
    if (mkdir_p(animate_dir) < 0 && errno != EEXIST) {
        fprintf(stderr, "dc_info_gen: mkdir %s: %s\n", animate_dir, strerror(errno));
        return 1;
    }

    char sprites[256][16];
    int  sprite_count = 0;
    collect_sprites(sprites, &sprite_count, 256);
    for (int i = 0; i < sprite_count; ++i)
        write_inc(animate_dir, sprites[i]);
    fprintf(stderr, "dc_info_gen: wrote %d .inc files to %s\n", sprite_count, animate_dir);

    /* 5. Update info.h statenum_t enum. */
    write_info_h_enum(info_h);
    fprintf(stderr, "dc_info_gen: updated statenum_t in %s\n", info_h);

    return 0;
}
