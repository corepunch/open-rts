#define _DEFAULT_SOURCE
#include "p_local.h"

static bool debug_effects_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("OPEN_RTS_DEBUG_EFFECTS");
        enabled = value && value[0] != '\0' && strcmp(value, "0") != 0;
    }
    return enabled != 0;
}

void debug_effects_log(const char *fmt, ...) {
    if (!debug_effects_enabled()) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[effects] ");
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int32_t read_i32_le(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool W_ReadFile(const char *path, blob_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return false;
    }
    rewind(fp);
    out->bytes = malloc((size_t)len);
    out->size = (size_t)len;
    if (!out->bytes) {
        fclose(fp);
        return false;
    }
    if (fread(out->bytes, 1, out->size, fp) != out->size) {
        free(out->bytes);
        memset(out, 0, sizeof(*out));
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

void W_FreeFile(blob_t *blob) {
    free(blob->bytes);
    memset(blob, 0, sizeof(*blob));
}

void M_PathJoin(char *dst, size_t dst_size, const char *a, const char *b) {
    snprintf(dst, dst_size, "%s/%s", a, b);
}

int clamp255(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

void V_IndexedToRGBA(uint32_t *dst, const uint8_t *src, size_t count, const uint32_t palette[256]) {
    for (size_t i = 0; i < count; ++i) dst[i] = palette[src[i]];
}

void V_BlitIndexed(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                          const uint8_t *src, int src_w, int src_h, const uint32_t palette[256]) {
    for (int y = 0; y < src_h; ++y) {
        int out_y = dst_y + y;
        if (out_y < 0 || out_y >= dst_h) continue;
        for (int x = 0; x < src_w; ++x) {
            int out_x = dst_x + x;
            if (out_x < 0 || out_x >= dst_w) continue;
            dst[out_y * dst_w + out_x] = palette[src[y * src_w + x]];
        }
    }
}

void HU_PushMessage(hudtext_t *hud, const char *text, int ttl_ms) {
    if (!hud || !text || text[0] == '\0') return;
    if (ttl_ms == 0) ttl_ms = 5000;
    int slot = hud->count;
    if (slot >= RTS_MAX_HUD_MESSAGES) {
        memmove(&hud->messages[0], &hud->messages[1],
                sizeof(hud->messages[0]) * (RTS_MAX_HUD_MESSAGES - 1));
        slot = RTS_MAX_HUD_MESSAGES - 1;
        hud->count = RTS_MAX_HUD_MESSAGES;
    } else {
        hud->count++;
    }
    snprintf(hud->messages[slot].text, sizeof(hud->messages[slot].text), "%s", text);
    hud->messages[slot].ttl_ms = ttl_ms;
}

void HU_Ticker(hudtext_t *hud, float dt) {
    if (!hud || hud->count <= 0) return;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < hud->count;) {
        if (hud->messages[i].ttl_ms < 0) {
            i++;
            continue;
        }
        hud->messages[i].ttl_ms -= dt_ms;
        if (hud->messages[i].ttl_ms <= 0) {
            memmove(&hud->messages[i], &hud->messages[i + 1],
                    sizeof(hud->messages[0]) * (size_t)(hud->count - i - 1));
            hud->count--;
        } else {
            i++;
        }
    }
}
