/* StarCraft IScript runtime. iscript.bin (PyMS IScriptBIN.py) holds one
 * program per image; an animation is an entry point into it. The control
 * flow and timing here follow OpenBW's iscript_execute: one call runs one
 * 24 Hz game frame, executing opcodes until a wait or an end; a wait of n
 * resumes n frames later; call keeps a single return address; every random
 * opcode draws from the synchronized LCG (OpenBW lcg_rand, here sc_random),
 * each with its own source index, in the order the script executes them.
 * Opcodes that act on the world are reported to the host in script order. */
#include "sc_local.h"
#include <stdlib.h>
#include <string.h>

/* Operand letters: H 16-bit, b signed byte, B byte, L label (16-bit file
 * offset), V a byte count and that many 16-bit sound rows. Names and operand
 * types are PyMS's OPCODES; the order is the opcode number. */
static const struct { const char *name, *args; } ops[SC_ISCRIPT_OPS] = {
    {"playfram","H"},{"playframtile","H"},{"sethorpos","b"},{"setvertpos","b"},{"setpos","bb"},
    {"wait","B"},{"waitrand","BB"},{"goto","L"},{"imgol","Hbb"},{"imgul","Hbb"},
    {"imgolorig","H"},{"switchul","H"},{"__0c",""},{"imgoluselo","Hbb"},{"imguluselo","Hbb"},
    {"sprol","Hbb"},{"highsprol","Hbb"},{"lowsprul","Hbb"},{"uflunstable","H"},{"spruluselo","Hbb"},
    {"sprul","Hbb"},{"sproluselo","HB"},{"end",""},{"setflipstate","B"},{"playsnd","H"},
    {"playsndrand","V"},{"playsndbtwn","HH"},{"domissiledmg",""},{"attackmelee","V"},{"followmaingraphic",""},
    {"randcondjmp","BL"},{"turnccwise","B"},{"turncwise","B"},{"turn1cwise",""},{"turnrand","B"},
    {"setspawnframe","B"},{"sigorder","B"},{"attackwith","B"},{"attack",""},{"castspell",""},
    {"useweapon","B"},{"move","B"},{"gotorepeatattk",""},{"engframe","B"},{"engset","B"},
    {"__2d",""},{"nobrkcodestart",""},{"nobrkcodeend",""},{"ignorerest",""},{"attkshiftproj","B"},
    {"tmprmgraphicstart",""},{"tmprmgraphicend",""},{"setfldirect","B"},{"call","L"},{"return",""},
    {"setflspeed","H"},{"creategasoverlays","B"},{"pwrupcondjmp","L"},{"trgtrangecondjmp","HL"},
    {"trgtarccondjmp","HHL"},{"curdirectcondjmp","HHL"},{"imgulnextid","bb"},{"__3e",""},
    {"liftoffcondjmp","L"},{"warpoverlay","H"},{"orderdone","B"},{"grdsprol","Hbb"},{"__43",""},
    {"dogrddamage",""},
};

const char *sc_iscript_op_name(unsigned op) { return op < SC_ISCRIPT_OPS ? ops[op].name : "?"; }

/* Operand bytes of the opcode at p, or -1 when it is unknown or runs past end. */
int sc_iscript_op_length(unsigned op, const uint8_t *p, size_t left) {
    if (op >= SC_ISCRIPT_OPS) return -1;
    int len = 0;
    for (const char *a = ops[op].args; *a; ++a) {
        if (*a == 'V') { if (left < 1) return -1; len += 1 + p[0] * 2; }
        else len += *a == 'H' || *a == 'L' ? 2 : 1;
    }
    return (size_t)len <= left ? len : -1;
}

/* Decode fixed operands into out (V yields its count alone); returns how many. */
static int decode(unsigned op, const uint8_t *p, int *out) {
    int n = 0;
    for (const char *a = ops[op].args; *a; ++a) {
        if (*a == 'V') { out[n++] = p[0]; break; }
        if (*a == 'H' || *a == 'L') { out[n++] = read_u16_le(p); p += 2; }
        else out[n++] = *a == 'b' ? (int8_t)*p++ : *p++;
    }
    return n;
}

/* Header layout is PyMS's: an offset table of (id, offset) pairs ending at
 * id 65535, each pointing at "SCPE", the animation-set type, and a table of
 * entry points whose length depends on the type. */
unsigned sc_iscript_animation(const blob_t *s, unsigned id, int anim) {
    if (s->size < 6) return 0;
    size_t table = read_u32_le(s->bytes + 2) == 0 ? read_u16_le(s->bytes) : 0;
    for (size_t at = table; at + 4 <= s->size; at += 4) {
        unsigned key = read_u16_le(s->bytes + at), off = read_u16_le(s->bytes + at + 2);
        if (key == 65535) break;
        if (key != id) continue;
        if (off + 8u > s->size || memcmp(s->bytes + off, "SCPE", 4)) return 0;
        unsigned type = s->bytes[off + 4];
        unsigned count = type <= 1 ? 2 : type == 2 ? 4 : type <= 13 ? 14 : type <= 15 ? 16 : type <= 21 ? 22 :
                         type == 23 ? 24 : type == 24 ? 26 : type <= 29 ? 28 : 0;
        if (anim < 0 || (unsigned)anim >= count || off + 8u + count * 2 > s->size) return 0;
        return read_u16_le(s->bytes + off + 8 + anim * 2);
    }
    return 0;
}

bool sc_iscript_walk(const blob_t *bin, unsigned start, uint8_t *seen, sc_iscript_visit_t visit, void *ctx) {
    unsigned stack[256];
    int sp = 0;
    if (start && start < bin->size) stack[sp++] = start;
    while (sp) {
        unsigned at = stack[--sp];
        for (;;) {
            if (at >= bin->size || seen[at]) break;
            seen[at] = 1;
            unsigned op = bin->bytes[at];
            int len = sc_iscript_op_length(op, bin->bytes + at + 1, bin->size - at - 1);
            if (len < 0) return false;
            const uint8_t *arg = bin->bytes + at + 1;
            if (visit) visit(ctx, at, op, arg, (unsigned)len);
            unsigned next = at + 1 + (unsigned)len;
            const char *a = ops[op].args;
            bool label = a[0] && a[strlen(a) - 1] == 'L';
            unsigned target = label ? read_u16_le(arg + len - 2) : 0;
            if (op == 22 || op == 54) break; /* end, return */
            if (op == 7) { at = target; continue; }
            if (label) { /* call, randcondjmp and the conditional jumps */
                if (sp == 256) return false;
                stack[sp++] = target;
            }
            at = next;
        }
    }
    return true;
}

bool sc_script_frame(const blob_t *bin, sc_script_t *c, const sc_script_host_t *h) {
    if (!c->active) return false;
    if (c->wait) { --c->wait; return true; }
    unsigned pc = c->pc;
    for (int steps = 0; steps < 4096; ++steps) {
        if (pc >= bin->size) break;
        unsigned op = bin->bytes[pc++];
        int len = sc_iscript_op_length(op, bin->bytes + pc, bin->size - pc);
        if (len < 0) break;
        const uint8_t *raw = bin->bytes + pc;
        int a[4] = {0};
        int n = decode(op, raw, a);
        pc += (unsigned)len;
        switch (op) {
        case 5: /* wait */
            c->wait = (uint16_t)(a[0] > 1 ? a[0] - 1 : 0);
            c->pc = (uint16_t)pc;
            return true;
        case 6: { /* waitrand: a + (rand & 0xff) % (b - a + 1) frames */
            unsigned r = h->random(h->ctx, 3) & 0xff;
            int span = a[1] - a[0] + 1, wait = a[0] + (span > 0 ? (int)(r % (unsigned)span) : 0) - 1;
            c->wait = (uint16_t)(wait > 0 ? wait : 0);
            c->pc = (uint16_t)pc;
            return true;
        }
        case 7: pc = (unsigned)a[0]; break;
        case 22:
            if (h->op) h->op(h->ctx, op, a, n);
            *c = (sc_script_t){0};
            return false;
        case 24: if (h->sound) h->sound(h->ctx, a[0]); break;
        case 25: case 28: case 26: {
            if (op == 28 && h->op) h->op(h->ctx, op, a, n);
            int row = 0;
            if (op == 26) {
                unsigned r = h->random(h->ctx, 5);
                row = a[0] + (a[1] >= a[0] ? (int)(r % (unsigned)(a[1] - a[0] + 1)) : 0);
            } else if (a[0] > 0) {
                unsigned r = h->random(h->ctx, 4);
                row = read_u16_le(raw + 1 + (r % (unsigned)a[0]) * 2);
            }
            if (row && h->sound) h->sound(h->ctx, row);
            break;
        }
        case 30: /* randcondjmp: jump when (rand & 0xff) <= chance */
            if ((h->random(h->ctx, 7) & 0xff) <= (unsigned)a[0]) pc = (unsigned)a[1];
            break;
        case 34: { /* turnrand: one draw in four turns the other way */
            int other = h->random(h->ctx, 6) % 4 == 1;
            int arg[2] = {a[0], other};
            if (h->op) h->op(h->ctx, op, arg, 2);
            break;
        }
        case 53: c->ret = (uint16_t)pc; pc = (unsigned)a[0]; break;
        case 54: pc = c->ret; break;
        case 48: /* ignorerest: waits ten frames at a time while the unit has a target */
            if (h->cond && h->cond(h->ctx, op, a)) { if (h->op) h->op(h->ctx, op, a, n); break; }
            c->wait = 10;
            c->pc = (uint16_t)(pc - 1);
            return true;
        case 57: case 58: case 59: case 60: case 63:
            if (h->cond && h->cond(h->ctx, op, a)) pc = (unsigned)a[n - 1];
            break;
        default:
            if (h->op) h->op(h->ctx, op, a, n);
            if (!c->active) return false; /* the host stopped the script */
        }
    }
    *c = (sc_script_t){0}; /* ran off the file or never waited: a malformed program stops */
    return false;
}

bool sc_script_start(const blob_t *bin, sc_script_t *c, unsigned script, int anim, const sc_script_host_t *h) {
    unsigned pc = sc_iscript_animation(bin, script, anim);
    *c = (sc_script_t){0};
    if (!pc || pc > UINT16_MAX || anim < 0 || anim > UINT8_MAX) return false;
    *c = (sc_script_t){.pc = (uint16_t)pc, .anim = (uint8_t)anim, .active = 1};
    return sc_script_frame(bin, c, h);
}

/* A saved cursor is idle with every field zero, or a running animation with a
 * program offset; the file is not needed to say so, and a pc that is not an
 * opcode stops the script the first frame it runs (sc_script_frame). */
bool sc_script_valid(const sc_script_t *c) {
    if (!c->active) return !c->pc && !c->ret && !c->wait && !c->anim;
    return c->active == 1 && c->anim < 29 && c->pc;
}

/* The engine asks each saved mobj (game/g_save.c parse_save). */
bool G_SavedObjectValid(const mobj_t *mo) {
    /* A pending hit needs a weapon row to deal it from. */
    if (mo->type_id == MT_SC_PROJECTILE && mo->sc.projectile.pending && !sc_weapon(mo->sc.projectile.weapon)) return false;
    return sc_script_valid(&mo->sc.script);
}

/* The retained program, and which unit and bullet image scripts play. */
static blob_t program;
static char program_root[1024];
int sc_unit_script[SC_TYPES];
int sc_weapon_script[SC_WEAPONS];

const blob_t *sc_iscript_bin(void) { return program.size ? &program : NULL; }

bool sc_iscript_load(const char *root) {
    if (program.size && !strcmp(program_root, root)) return true;
    W_FreeFile(&program);
    program = (blob_t){0};
    if (!sc_read(root, "scripts/iscript.bin", &program)) return false;
    snprintf(program_root, sizeof(program_root), "%s", root);
    sc_handover_load(root, &program);
    return true;
}

static uint32_t host_random(void *ctx, int source) { (void)ctx; (void)source; return sc_random(); }
/* A pending hit's sound is the weapon's own, played when it fires (sounds.c). */
static void host_sound(void *ctx, int row) {
    const mobj_t *mo = ctx;
    if (mo->type_id == MT_SC_PROJECTILE && mo->sc.projectile.pending) return;
    sc_script_play(ctx, row);
}
static bool host_cond(void *ctx, int op, const int *arg) {
    const mobj_t *mo = ctx;
    (void)arg;
    /* The host has no target-aim, image or power-up state to test: only the
     * building's flight and the unit's idle path (ignorerest) are answered. */
    return op == 63 ? (mo->traits & MF_FLY) != 0 : op == 48;
}
/* An animation that signals its order done (sigorder) has handed back to the
 * idle loops that follow it, which this runtime does not play: the cursor stops. */
static void host_op(void *ctx, int op, const int *arg, int n) {
    (void)arg; (void)n;
    if (op == 36) ((mobj_t *)ctx)->sc.script = (sc_script_t){0};
    else if (op == 27 || op == 68) sc_projectile_script_hit(ctx); /* domissiledmg, dogrddamage */
}
static const sc_script_host_t mobj_host_template = {host_random, host_op, host_cond, host_sound, NULL};

void sc_script_begin(mobj_t *mo, int script, int anim) {
    const blob_t *bin = sc_iscript_bin();
    sc_script_host_t host = mobj_host_template;
    host.ctx = mo;
    mo->sc.script = (sc_script_t){0};
    if (bin && script >= 0) sc_script_start(bin, &mo->sc.script, (unsigned)script, anim, &host);
}

void sc_script_tick(mobj_t *mo, int frames) {
    const blob_t *bin = sc_iscript_bin();
    if (!mo->sc.script.active || !bin) return;
    sc_script_host_t host = mobj_host_template;
    host.ctx = mo;
    while (frames-- > 0 && sc_script_frame(bin, &mo->sc.script, &host)) {}
}
