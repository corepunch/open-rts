/* The frames a Protoss or Zerg foundation spends in its script-signalled
 * hand-over, read from iscript.bin. OpenBW's order_IncompleteWarping (states 1
 * to 3) and order_IncompleteMorphing (state 6) each wait for an IScript
 * sigorder bit before the building completes; G_ModelUpdateConstruction in
 * p_prod.c counts these frames instead of running the building's images.
 *
 * Every number comes from the real scripts and OpenBW's frame order (units
 * run their order, then their sprite's image scripts, in each frame):
 *
 *  Zerg, state 5 runs AlmostBuilt on the construction image (units.dat
 *  construction_animation) at frame F; script call j of an animation started
 *  by sprite_run_anim executes in frame F + j - 1 (call 0 is the start itself,
 *  call 1 follows in the same frame's sprite pass); the order sees the signal
 *  in the frame after, so state 6 completes at F + max(j, 1) for a sigorder on
 *  call j. State 5 is one frame after the timer's end frame T (state 4), so
 *  completion is T + 1 + max(j, 1).
 *
 *  Protoss, state 0 starts SpecialState1 on the Warp Anchor at T and state 1
 *  takes its signal at T + max(j1, 1). It then starts the Warp Texture script
 *  (193) Init with two calls in the order's own frame; its sigorder on call j2
 *  is seen max(j2 - 1, 1) frames later by state 2, which starts WarpIn. WarpIn
 *  creates the warp-flash image, whose draw function 17 runs
 *  image_update_warpin: data1 starts at 48 and data2 at 2, data2 counts down
 *  and then reloads to 2 while data1 rises to 63, and the first update after
 *  that signals; state 3 sees it the next frame and completes. */
#include "sc_local.h"
#include <stdlib.h>
#include <string.h>

enum { WARP_ANCHOR_IMAGE = 211, WARP_TEXTURE_SCRIPT = 193, WARP_FLASH_DRAW = 17,
       ANIM_INIT = 0, ANIM_SPECIAL1 = 13, ANIM_ALMOST_BUILT = 15, ANIM_WARP_IN = 21,
       UNITS_CONSTRUCTION_COLUMN = 1332, /* PyMS UnitsDAT: after 228 + 2 * 228 + 2 * 228 + 2 * 96 bytes */
       PROBE_LIMIT = 400 };

static int hand_over[SC_TYPES];

int sc_hand_over(int type_id) { return type_id >= 1 && type_id <= SC_TYPES ? hand_over[type_id - 1] : 0; }

typedef struct { int calls, sig_call, image; unsigned mask; bool random; } probe_t;
static uint32_t probe_random(void *ctx, int source) { (void)source; ((probe_t *)ctx)->random = true; return 0; }
static void probe_op(void *ctx, int op, const int *arg, int n) {
    probe_t *p = ctx;
    (void)n;
    if (op == 36 && p->sig_call < 0 && (arg[0] & p->mask)) p->sig_call = p->calls; /* sigorder */
    else if (op == 8 && p->image < 0) p->image = arg[0];                           /* imgol */
}

/* The call on which the animation first signals one of the mask bits (call 0 is
 * the start), and the first image it overlays; -1 when it never does or when
 * the path draws a random number. */
static int probe(const blob_t *bin, unsigned script, int anim, unsigned mask, int *image) {
    probe_t p = {.sig_call = -1, .image = -1, .mask = mask};
    const sc_script_host_t host = {probe_random, probe_op, NULL, NULL, &p};
    sc_script_t c;
    sc_script_start(bin, &c, script, anim, &host);
    for (p.calls = 1; p.calls < PROBE_LIMIT && c.active; ++p.calls) sc_script_frame(bin, &c, &host);
    if (image) *image = p.image;
    return p.random ? -1 : p.sig_call;
}

/* OpenBW image_update_warpin from a fresh draw-function-17 image: the frame
 * of its image pass that signals (0 is the frame it was created in). */
static int warp_counter_frames(void) {
    int data1 = 48, data2 = 2;
    for (int frame = 0; frame < 1000; ++frame) {
        if (data2) --data2;
        else {
            data2 = 2;
            if (data1 < 63) ++data1;
            else return frame;
        }
    }
    return -1;
}

void sc_handover_load(const char *root, const blob_t *bin) {
    blob_t units = {0}, flingy = {0}, sprites = {0}, images = {0};
    memset(hand_over, 0, sizeof(hand_over));
    if (!bin || !sc_read(root, "arr/units.dat", &units) || !sc_read(root, "arr/flingy.dat", &flingy) ||
        !sc_read(root, "arr/sprites.dat", &sprites) || !sc_read(root, "arr/images.dat", &images)) goto done;
    unsigned ni = (unsigned)(images.size / 38);
    if (images.size % 38 || units.size < (size_t)UNITS_CONSTRUCTION_COLUMN + SC_TYPES * 4 || WARP_ANCHOR_IMAGE >= ni) goto done;
    #define IMAGE_SCRIPT(im) read_u32_le(images.bytes + ni * 10 + (im) * 4)
    #define IMAGE_FULL(im) images.bytes[ni * 6 + (im)]
    #define IMAGE_DRAW(im) images.bytes[ni * 8 + (im)]
    int j1 = IMAGE_FULL(WARP_ANCHOR_IMAGE) ? probe(bin, IMAGE_SCRIPT(WARP_ANCHOR_IMAGE), ANIM_SPECIAL1, 1, NULL) : -1;
    int j2 = probe(bin, WARP_TEXTURE_SCRIPT, ANIM_INIT, 1, NULL), flash = warp_counter_frames();
    for (int t = 0; t < SC_TYPES; ++t) {
        /* Zerg: the construction image's AlmostBuilt signals bit 4. */
        unsigned construction = read_u32_le(units.bytes + UNITS_CONSTRUCTION_COLUMN + t * 4);
        if (construction && construction < ni && IMAGE_FULL(construction)) {
            int j = probe(bin, IMAGE_SCRIPT(construction), ANIM_ALMOST_BUILT, 4, NULL);
            if (j >= 0) { hand_over[t] = 1 + (j > 1 ? j : 1); continue; }
        }
        /* Protoss: the building's WarpIn overlays a flash drawn with the warp-in function. */
        unsigned fl = units.bytes[t], sp = fl * 2 + 2 <= flingy.size ? read_u16_le(flingy.bytes + fl * 2) : 0xffff;
        unsigned im = sp * 2 + 2 <= sprites.size ? read_u16_le(sprites.bytes + sp * 2) : ni;
        if (im >= ni || !IMAGE_FULL(im) || j1 < 0 || j2 < 0 || flash < 0) continue;
        int overlay;
        probe(bin, IMAGE_SCRIPT(im), ANIM_WARP_IN, 0, &overlay);
        if (overlay >= 0 && (unsigned)overlay < ni && IMAGE_DRAW(overlay) == WARP_FLASH_DRAW)
            hand_over[t] = (j1 > 1 ? j1 : 1) + (j2 - 1 > 1 ? j2 - 1 : 1) + flash + 1;
    }
    #undef IMAGE_SCRIPT
    #undef IMAGE_FULL
    #undef IMAGE_DRAW
done:
    W_FreeFile(&units); W_FreeFile(&flingy); W_FreeFile(&sprites); W_FreeFile(&images);
}
