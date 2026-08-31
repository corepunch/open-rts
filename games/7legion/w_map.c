#define _DEFAULT_SOURCE
#include "engine.h"
#include "sl_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int map_number;
    int terrain;
    int start_x;
    int start_y;
    int start_cash;
    char player_start[64];
} SlMissionConfig;

static bool sl_sibling_path(char *out, size_t out_size,
                            const char *path, const char *name) {
    const char *slash = strrchr(path, '/');
    if (!slash) return snprintf(out, out_size, "%s", name) < (int)out_size;
    size_t prefix = (size_t)(slash - path + 1);
    if (prefix + strlen(name) + 1 > out_size) return false;
    memcpy(out, path, prefix);
    snprintf(out + prefix, out_size - prefix, "%s", name);
    return true;
}

static bool sl_load_first_mission_config(const char *map_path, SlMissionConfig *out) {
    memset(out, 0, sizeof(*out));
    out->terrain = 2;
    out->start_x = 84;
    out->start_y = 74;
    out->start_cash = 15000;
    snprintf(out->player_start, sizeof(out->player_start),
             "0000000000000040000000000000000000000000001");

    char path[512];
    if (!sl_sibling_path(path, sizeof(path), map_path, "Missions.ini")) return false;
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); return false; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);

    char *section = strstr(text, "[GMission 1]");
    if (!section) { free(text); return false; }
    char *next_section = strstr(section + 1, "\n[");
    if (next_section) *next_section = '\0';
    char *save = NULL;
    for (char *line = strtok_r(section, "\r\n", &save);
         line; line = strtok_r(NULL, "\r\n", &save)) {
        int value = 0;
        if (sscanf(line, "Map=%d", &value) == 1) out->map_number = value;
        else if (sscanf(line, "Terrain=%d", &value) == 1) out->terrain = value;
        else if (sscanf(line, "StartX1=%d", &value) == 1) out->start_x = value;
        else if (sscanf(line, "StartY1=%d", &value) == 1) out->start_y = value;
        else if (sscanf(line, "StartCash=%d", &value) == 1) out->start_cash = value;
        else if (strncmp(line, "PVStart=", 8) == 0) {
            size_t length = strcspn(line + 8, " ;\t");
            if (length >= sizeof(out->player_start)) length = sizeof(out->player_start) - 1;
            memcpy(out->player_start, line + 8, length);
            out->player_start[length] = '\0';
        }
    }
    free(text);
    return true;
}

static const char *sl_tileset_for_terrain(int terrain) {
    switch (terrain) {
    case 0: return "GFX/TILES.BIM";
    case 1: return "GFX/TILES1.BIM";
    case 2: return "GFX/TILES2.BIM";
    default: return "GFX/TILES3.BIM";
    }
}

bool sl_load_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));

    blob_t tiles;
    if (!W_ReadFile(map_path, &tiles)) return false;
    const int W = 128, H = 128;
    if (tiles.size != (size_t)W * H * 2) {
        fprintf(stderr, "7legion: %s: expected a 128x128 MAPT layer\n", map_path);
        W_FreeFile(&tiles);
        return false;
    }
    out->width  = W;
    out->height = H;

    out->tile_ids = calloc((size_t)W * H, sizeof(uint16_t));
    out->blocked  = calloc((size_t)W * H, sizeof(uint8_t));
    out->tile_flip_flags[0] = calloc((size_t)W * H, sizeof(uint8_t));
    if (!out->tile_ids || !out->blocked || !out->tile_flip_flags[0]) {
        free(out->tile_ids);
        free(out->blocked);
        free(out->tile_flip_flags[0]);
        W_FreeFile(&tiles);
        return false;
    }
    const uint8_t *tile_bytes = (const uint8_t *)tiles.bytes;
    uint16_t key = 30000;
    /* MAPT is decoded in file order by legion.exe, but its world access is
       column-major: file cell (x,y) is displayed at world index (x*128+y).
       Keeping that native addressing is what makes coast/road transitions
       meet in the correct direction. */
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int source_i = y * W + x;
            int output_i = x * H + y;
            uint16_t stored = read_u16_le(tile_bytes + (size_t)source_i * 2);
            out->tile_ids[output_i] = (uint16_t)((stored ^ key) - x);
            key--;
        }
    }
    W_FreeFile(&tiles);

    /* MAPOVL is a sparse 16-bit overlay layer.  The low byte identifies the
       overlay tile/type; the high byte is native placement metadata.  Keep
       the metadata separate from the tile id: the original renderer reads
       both bytes independently when placing the overlay. */
    char overlay_path[512];
    if (sl_sibling_path(overlay_path, sizeof(overlay_path), map_path, "MAPOVL.000")) {
        blob_t overlay = { 0 };
        if (W_ReadFile(overlay_path, &overlay) && overlay.size == (size_t)W * H * 2) {
            out->tile_overlays[0] = calloc((size_t)W * H, sizeof(uint16_t));
            if (out->tile_overlays[0]) {
                const uint8_t *p = (const uint8_t *)overlay.bytes;
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        int source_i = y * W + x;
                        int output_i = x * H + y;
                        out->tile_overlays[0][output_i] =
                            (uint16_t)(read_u16_le(p + (size_t)source_i * 2) & 0xffu);
                    }
                }
                out->tile_overlay_count = 1;
                out->render_capabilities |= MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS;
            }
        }
        W_FreeFile(&overlay);
    }

    char land_path[512];
    if (sl_sibling_path(land_path, sizeof(land_path), map_path, "MAPL.000")) {
        blob_t land;
        if (W_ReadFile(land_path, &land)) {
            if (land.size == (size_t)W * H) {
                const uint8_t *values = (const uint8_t *)land.bytes;
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        int source_i = y * W + x;
                        int output_i = x * H + y;
                        out->blocked[output_i] = (values[source_i] ^ y) != 0;
                    }
                }
            }
            W_FreeFile(&land);
        }
    }

    SlMissionConfig mission;
    sl_load_first_mission_config(map_path, &mission);
    snprintf(out->tileset_name, sizeof(out->tileset_name), "%s",
             sl_tileset_for_terrain(mission.terrain));
    out->player_resources[0][0] = mission.start_cash;

    out->direction_mode = RTS_DIRECTION_DARK_REIGN_8;
    out->has_camera = true;
    out->camera_gx  = (float)mission.start_x;
    out->camera_gy  = (float)mission.start_y;
    return true;
}
int sl_load_initial_units(const char *map_path, mobj_t *units, int max_units) {
    if (!units || max_units <= 0) return 0;
    SlMissionConfig mission;
    if (!sl_load_first_mission_config(map_path, &mission)) return 0;

    int count = 0;
    int troop_count = strlen(mission.player_start) > 14 ? mission.player_start[14] - '0' : 0;
    int base_count = strlen(mission.player_start) > 42 ? mission.player_start[42] - '0' : 0;
    for (int i = 0; i < troop_count && count < max_units; ++i) {
        mobj_t *unit = &units[count++];
        memset(unit, 0, sizeof(*unit));
        unit->core.gx = (float)(mission.start_x - 6 + i * 2) + 0.5f;
        unit->core.gy = (float)(mission.start_y - 1) + 0.5f;
        unit->owner = 0;
        unit->type_id = 1;
        unit->core.facing_code = 8;
    }
    for (int i = 0; i < base_count && count < max_units; ++i) {
        mobj_t *unit = &units[count++];
        memset(unit, 0, sizeof(*unit));
        unit->core.gx = (float)(mission.start_x + 4 + i * 2) + 0.5f;
        unit->core.gy = (float)(mission.start_y + 1) + 0.5f;
        unit->owner = 0;
        unit->type_id = 7;
        unit->core.facing_code = 8;
    }
    return count;
}
