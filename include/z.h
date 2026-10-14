#ifndef __Z__
#define __Z__

#include "engine.h"

/* Z (The Bitmap Brothers, 1996). Maps are up to 128x128 cells of 16x16
 * pixels; see docs/Z_FORMATS.md. */
enum { Z_TILE = 16, Z_MAP_STRIDE = 128 };

/* One member of a JMP2 archive (WARDATA.PAC and friends), decompressed. */
bool Z_ReadPac(const char *path, const char *name, blob_t *out);
/* LZSS as ZED.EXE unpacks PAC members; false if `src` runs short. */
bool Z_Unpack(const uint8_t *src, size_t size, uint8_t *dst, size_t length);

#endif
