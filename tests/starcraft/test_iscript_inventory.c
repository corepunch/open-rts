/* Which IScript opcodes the real iscript.bin uses for every image the game
 * draws: units, weapon bullets, and the images and sprites their scripts
 * spawn in turn. Counts are instruction sites reachable from any animation
 * entry point, each site counted once. Run with --table to print the
 * markdown table kept in docs/SC_PROJECTILE_VISUALS.md. */
#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft IScript inventory", #c)

static const char *root = "data/STARCRAFT";
static blob_t units, flingy, sprites, images, weapons, bin;
static unsigned ni, nf;
static uint8_t *seen;
static bool image_done[4096];
static unsigned pending[4096];
static int pending_count;
static int uses[SC_ISCRIPT_OPS];
static unsigned call_site[16], call_target[16], return_site;
static int calls;
static int zero_waits, inverted_waitrands, scripts_walked, images_walked;

/* How w_assets.c compiles an opcode: the visual states it builds, or the
 * runtime in p_iscript.c, which executes every opcode below the line. */
static const char *const ASSET_STATUS[SC_ISCRIPT_OPS] = {
    [0] = "yes", [1] = "mis-handled: frame without the tileset offset",
    [5] = "yes", [6] = "mis-handled: compiled as a wait of its minimum",
    [7] = "yes", [22] = "yes", [30] = "ignored: the jump is never taken", [53] = "yes", [54] = "yes",
    [8] = "partial: first image of a death or bullet script", [9] = "ignored",
    [13] = "partial: as imgol", [14] = "ignored",
    [15] = "partial: first sprite of a death, trail or impact", [16] = "partial: as sprol",
    [17] = "partial: as sprol", [19] = "partial: as sprol", [20] = "partial: as sprol",
    [21] = "partial: as sprol", [66] = "partial: as sprol",
    [27] = "yes: strike", [28] = "yes: strike", [37] = "yes: strike", [38] = "yes: strike",
    [40] = "yes: strike", [68] = "yes: strike",
    [42] = "partial: ends a worker swing", [48] = "partial: ends the path", [52] = "partial: frames stay unflipped",
    [24] = "ignored: first sound only, in sounds.c", [25] = "ignored: first sound only, in sounds.c",
    [26] = "ignored: first sound only, in sounds.c",
    [57] = "ignored: never taken", [58] = "ignored: never taken", [59] = "ignored: never taken",
    [60] = "ignored: never taken", [63] = "ignored: never taken",
};

/* What p_iscript.c does with an opcode. */
static const char *runtime(unsigned op) {
    switch (op) {
    case 5: case 6: case 7: case 22: case 30: case 53: case 54: return "executed (OpenBW timing)";
    case 24: case 25: case 26: case 28: return "executed, plays its row";
    case 34: return "executed, draws its roll";
    case 36: return "executed, ends the game cursor";
    case 48: case 63: return "branch answered by the game";
    case 57: case 58: case 59: case 60: return "branch never taken";
    default: return "reported, not acted on";
    }
}
static void queue_image(unsigned image) {
    if (image >= ni || image >= 4096 || image_done[image]) return;
    image_done[image] = true;
    pending[pending_count++] = image;
}
static void queue_sprite(unsigned sprite) {
    if (sprite * 2 + 2 <= sprites.size) queue_image(read_u16_le(sprites.bytes + sprite * 2));
}
static void queue_flingy(unsigned f) {
    if (f < nf) queue_sprite(read_u16_le(flingy.bytes + f * 2));
}
static unsigned current_image;
static void visit(void *ctx, unsigned pc, unsigned op, const uint8_t *arg, unsigned len) {
    (void)ctx; (void)pc; (void)len;
    ++uses[op];
    switch (op) {
    case 53: if (calls < 16) { call_site[calls] = pc; call_target[calls++] = read_u16_le(arg); } break;
    case 54: return_site = pc; break;
    case 5: zero_waits += arg[0] == 0; break;
    case 6: inverted_waitrands += arg[1] < arg[0]; break;
    case 8: case 9: case 13: case 14: queue_image(read_u16_le(arg)); break;
    case 10: case 11: queue_image(read_u16_le(arg)); break;
    case 61: queue_image(current_image + 1); break;
    case 15: case 16: case 17: case 19: case 20: case 21: case 66: queue_sprite(read_u16_le(arg)); break;
    case 18: queue_flingy(read_u16_le(arg)); break;
    case 40: {
        unsigned nw = (unsigned)(weapons.size / 42), w = arg[0];
        if (w < nw) queue_flingy(read_u32_le(weapons.bytes + 2 * nw + 4 * w));
        break;
    }
    }
}

static bool walk_all(void) {
    for (int i = 0; i < SC_TYPES; ++i) queue_flingy(units.bytes[i]);
    for (int w = 0; w < SC_WEAPONS; ++w) queue_flingy((unsigned)sc_weapons[w].flingy);
    while (pending_count) {
        current_image = pending[--pending_count];
        unsigned script = read_u32_le(images.bytes + ni * 10 + current_image * 4);
        ++images_walked;
        for (int anim = 0; anim < 29; ++anim) {
            unsigned start = sc_iscript_animation(&bin, script, anim);
            if (start && !sc_iscript_walk(&bin, start, seen, visit, NULL)) return false;
        }
        ++scripts_walked;
    }
    return true;
}

/* The script frame on which a bullet's Death first deals its damage (domissiledmg), or -1. */
static int hit_frame, hit_now;
static uint32_t quiet_random(void *ctx, int source) { (void)ctx; (void)source; return 0; }
static void note_hit(void *ctx, int op, const int *arg, int n) {
    (void)ctx; (void)arg; (void)n;
    if ((op == 27 || op == 68) && hit_frame < 0) hit_frame = hit_now;
}
static int death_hit_frame(int weapon) {
    unsigned f = (unsigned)sc_weapons[weapon].flingy, sprite, image;
    if (f >= nf || (sprite = read_u16_le(flingy.bytes + f * 2)) * 2 + 2 > sprites.size ||
        (image = read_u16_le(sprites.bytes + sprite * 2)) >= ni) return -2;
    const sc_script_host_t host = {quiet_random, note_hit, NULL, NULL, NULL};
    sc_script_t c;
    hit_frame = -1;
    hit_now = 0;
    sc_script_start(&bin, &c, read_u32_le(images.bytes + ni * 10 + image * 4), SC_ANIM_DEATH, &host);
    for (hit_now = 1; hit_now < 100 && c.active; ++hit_now) sc_script_frame(&bin, &c, &host);
    return hit_frame;
}

int main(int argc, char **argv) {
    CHECK(sc_read(root, "arr/units.dat", &units) && sc_read(root, "arr/flingy.dat", &flingy) &&
          sc_read(root, "arr/sprites.dat", &sprites) && sc_read(root, "arr/images.dat", &images) &&
          sc_read(root, "arr/weapons.dat", &weapons) && sc_read(root, "scripts/iscript.bin", &bin));
    ni = (unsigned)(images.size / 38); nf = (unsigned)(flingy.size / 15);
    seen = calloc(bin.size, 1);
    CHECK(seen && walk_all());
    int total = 0, kinds = 0;
    for (unsigned op = 0; op < SC_ISCRIPT_OPS; ++op) { total += uses[op]; kinds += uses[op] > 0; }
    printf("StarCraft IScript: %d images, %d instruction sites, %d opcodes in use\n", images_walked, total, kinds);
    if (argc > 1 && !strcmp(argv[1], "--table")) {
        puts("| Opcode | Name | Uses | w_assets.c (visual states) | p_iscript.c (runtime) |\n|---|---|---|---|---|");
        for (unsigned op = 0; op < SC_ISCRIPT_OPS; ++op)
            if (uses[op]) printf("| 0x%02x | %s | %d | %s | %s |\n", op, sc_iscript_op_name(op), uses[op],
                                 ASSET_STATUS[op] ? ASSET_STATUS[op] : "ignored", runtime(op));
    }
    /* Every opcode the data uses has a decoded length (the walk would fail
     * otherwise); waits are never zero and random waits never inverted, the
     * two cases where OpenBW's arithmetic leaves its defined range. */
    CHECK(zero_waits == 0 && inverted_waitrands == 0);
    /* OpenBW keeps one return address: calls here are safe only if none sits inside the
     * subroutine another call enters. */
    CHECK(calls == 6 && uses[54] == 1);
    for (int i = 0; i < calls; ++i)
        for (int j = 0; j < calls; ++j) CHECK(!(call_site[j] >= call_target[i] && call_site[j] <= return_site));
    CHECK(uses[5] > 1000 && uses[0] > 1000 && uses[22] > 100);
    /* Damage frame of every flying, homing and bouncing bullet: the first frame of its Death,
     * which is when p_combat.c deals it at impact. Bullets that appear on the target (behavior
     * 2) and appear-at-source (5) bullets wait in their Death before dealing damage. */
    for (int w = 0; w < SC_WEAPONS; ++w) {
        if (sc_weapons[w].damage <= 0 || sc_weapons[w].flingy <= 0) continue;
        int b = sc_weapons[w].behavior;
        if (b == 0 || b == 1 || b == SC_BEHAVIOR_BOUNCE) CHECK(death_hit_frame(w) == 0);
    }
    CHECK(death_hit_frame(27) == 1 && death_hit_frame(28) == 1); /* Arclite Shock Cannon */
    CHECK(death_hit_frame(70) == 3 && death_hit_frame(71) == 3); /* Psionic Shockwave */
    CHECK(death_hit_frame(53) == 5 && death_hit_frame(45) == 5); /* Subterranean Tentacle, Acid Spray */
    CHECK(death_hit_frame(0) == 0 && death_hit_frame(25) == 5);  /* Gauss Rifle, Flame Thrower */
    puts("PASS: iscript.bin opcode inventory");
    return 0;
}
