#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "m_vec.h"
#include "sprites.h"

#include <stdbool.h>
#include <stdint.h>

#define SCREENWIDTH  640
#define SCREENHEIGHT 480

enum {
    V_FLIP_X = 1u << 0,
    V_FLIP_Y = 1u << 1,
    /* Write index 0. Sprites skip it; opaque backgrounds do not. */
    V_OPAQUE = 1u << 2
};

typedef struct {
    uint8_t *pixels; /* w*h palette indices, row-major, pitch == w */
    int w, h;
} vscreen_t;

extern vscreen_t screens[1];
extern uint32_t vpalette[256];

void V_AllocScreen(int w, int h);
void V_FreeScreen(void);
void V_BeginFrame(uint32_t clear_argb);
void I_SetPalette(const uint32_t argb[256]);
bool I_ReadScreen(uint8_t *dst);
uint8_t V_NearestIndex(uint32_t argb);
/* Identity when the palette matches the screen. Index 0 stays 0. */
const uint8_t *V_RemapPalette(const uint32_t palette[256]);
void V_ComposeRemap(uint8_t out[256], const uint32_t palette[256],
                    const uint8_t *team, int intensity);
/* Multiply source RGB by an 0xAARRGGBB color, then nearest-match the screen. */
void V_ModulateRemap(uint8_t out[256], const uint32_t palette[256], uint32_t color);
void V_ReadPixels(uint32_t *dst, int dst_pitch_bytes);

void V_SetClip(irect_t clip);
irect_t V_GetClip(void);

void V_FillRect(irect_t r, uint8_t color);
void V_DrawLine(ivec2_t a, ivec2_t b, uint8_t color);
void V_DrawPoint(ivec2_t p, uint8_t color);
void V_DrawRectOutline(irect_t r, uint8_t color);
void V_DrawBlock(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                 const uint8_t *remap, uint32_t flags);
void V_DrawBlockScaled(irect_t dst, const uint8_t *src, isize2_t size, int src_pitch,
                       const uint8_t *remap, uint32_t flags);
void V_DrawBlockTranslucent(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                            const uint8_t *table, uint32_t flags);
void V_DrawSilhouetteColormap(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                              const uint8_t *colormap, uint32_t flags);
void V_CopyRect(irect_t src, irect_t dst);

void V_DrawSpriteCell(ivec2_t at, const spritesheet_t *sheet, int cell,
                      const uint8_t *remap, uint32_t flags);
void V_DrawSpriteCellScaled(irect_t dst, const spritesheet_t *sheet, int cell,
                            const irect_t *src, const uint8_t *remap, uint32_t flags);

int V_TextWidth(const bitmapfont_t *font, const char *text);
void V_DrawText(ivec2_t at, const bitmapfont_t *font, const char *text, const uint8_t *remap);
void V_DrawTextScaled(ivec2_t at, const bitmapfont_t *font, const char *text,
                      const uint8_t *remap, int scale);
void V_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                       const uint8_t *remap, int scroll_px);

bool R_DrawIndexed(const uint8_t *indices, isize2_t size, const uint32_t palette[256],
                   const irect_t *src, const irect_t *dst, uint32_t flags);
bool R_DrawSprite(const spritesheet_t *sprite, int frame, int palette,
                  const irect_t *src, const irect_t *dst, uint32_t flags, int intensity);

#endif
