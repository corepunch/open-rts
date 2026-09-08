#include "w_sprite_private.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

bool DC_AnimationPath(char *out, size_t out_size,
                                                  const char *sprite_path) {
    if (!out || out_size == 0 || !sprite_path) return false;
    const char *base = strrchr(sprite_path, '/');
    base = base ? base + 1 : sprite_path;
    const char *dot = strrchr(base, '.');
    if (!dot || strcasecmp(dot, ".SPR") != 0) return false;

    const char *dir_end = base > sprite_path ? base - 1 : NULL;
    const char *dir_start = dir_end;
    while (dir_start && dir_start > sprite_path && dir_start[-1] != '/') dir_start--;
    size_t dir_len = dir_start ? (size_t)(dir_end - dir_start) : 0;
    if (!dir_start || dir_len != strlen("SPRITES") ||
        strncasecmp(dir_start, "SPRITES", dir_len) != 0) {
        return false;
    }

    size_t prefix_len = (size_t)(dir_start - sprite_path);
    size_t stem_len = (size_t)(dot - base);
    if (prefix_len + strlen("ANIMATE/") + stem_len + strlen(".FIN") + 1 > out_size)
        return false;
    memcpy(out, sprite_path, prefix_len);
    out[prefix_len] = '\0';
    strncat(out, "ANIMATE/", out_size - strlen(out) - 1);
    strncat(out, base, stem_len);
    strncat(out, ".FIN", out_size - strlen(out) - 1);
    return true;
}

bool DC_SpritePath(char *out, size_t out_size,
                                      const char *animation_path) {
    if (!out || out_size == 0 || !animation_path) return false;
    const char *base = strrchr(animation_path, '/');
    base = base ? base + 1 : animation_path;
    const char *dot = strrchr(base, '.');
    if (!dot || strcasecmp(dot, ".FIN") != 0) return false;

    const char *dir_end = base > animation_path ? base - 1 : NULL;
    const char *dir_start = dir_end;
    while (dir_start && dir_start > animation_path && dir_start[-1] != '/') dir_start--;
    size_t dir_len = dir_start ? (size_t)(dir_end - dir_start) : 0;
    if (!dir_start || dir_len != strlen("ANIMATE") ||
        strncasecmp(dir_start, "ANIMATE", dir_len) != 0) return false;

    size_t prefix_len = (size_t)(dir_start - animation_path);
    size_t stem_len = (size_t)(dot - base);
    return snprintf(out, out_size, "%.*sSPRITES/%.*s.SPR",
                    (int)prefix_len, animation_path, (int)stem_len, base) < (int)out_size;
}

bool DC_DependencySpriteName(char *out, size_t out_size,
                                               const char *dependency) {
    if (!out || out_size == 0 || !dependency) return false;
    char stem[16];
    size_t len = 0;
    while (dependency[len] != '\0' && len < 8 &&
           !isspace((unsigned char)dependency[len])) {
        unsigned char ch = (unsigned char)dependency[len];
        if (!isalnum(ch) && ch != '_') return false;
        stem[len++] = (char)toupper(ch);
    }
    if (len == 0) return false;
    stem[len] = '\0';
    return snprintf(out, out_size, "SPRITES/%s.SPR", stem) < (int)out_size;
}

void DC_SpriteStem(char *out, size_t out_size, const char *sprite_path) {
    const char *base = strrchr(sprite_path, '/');
    base = base ? base + 1 : sprite_path;
    size_t length = strcspn(base, ".");
    if (length >= out_size) length = out_size - 1;
    for (size_t i = 0; i < length; ++i)
        out[i] = (char)tolower((unsigned char)base[i]);
    out[length] = '\0';
}

bool DC_AssetExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

