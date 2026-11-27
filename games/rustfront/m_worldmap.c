#include "engine.h"
#include "rustfront.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The campaign's world map, in the manner of Dune 2000's (art/worldmap.py): the Rustfront cut
 * into territories, each tinted with its holder's colour as the campaign stands, and the
 * campaign's five missions marked where they are fought. Winning a mission takes its territory;
 * winning the campaign takes the map.
 *
 *   assets/worldmap.pcx       the painted map, 640 x 480, the page's background and palette
 *   assets/worldmap_mask.pcx  each pixel's territory (255: none)
 *   assets/worldmap.txt       the territories (seat, holder at the start, name), the missions'
 *                             territories, and the holders' tints
 */

enum { MAX_TERRITORIES = 32, HOLDERS = 1 + RF_FACTION_COUNT };

typedef struct { char name[32]; ivec2_t seat; int holder; } territory_t;

static territory_t territories[MAX_TERRITORIES];
static int territory_count;
static int mission_at[RF_FACTION_COUNT][RF_CAMPAIGN_MISSIONS];
static uint32_t tint_rgb[HOLDERS];
static int tint_amount[HOLDERS];
static spritesheet_t sheet;
static SDL_Surface *base, *mask;
static bool loaded;

/* The tinted layer at the map's own size: 0 leaves the painted map, else the index to draw. */
static uint8_t *layer;
static int layer_key = -1;
static const uint32_t *layer_palette;

static int holder_index(const char *word) {
    if (!strcmp(word, "directorate")) return 1;
    if (!strcmp(word, "clans")) return 2;
    if (!strcmp(word, "concord")) return 3;
    return 0;
}

bool RF_LoadWorldMap(const char *root) {
    if (loaded) return true;
    char path[1024];
    root = root ? root : g_game_default_root;
    snprintf(path, sizeof(path), "%s/assets/worldmap.pcx", root);
    if (!W_LoadIndexedSheet(path, &sheet)) return false;
    base = W_LoadImage(path);
    snprintf(path, sizeof(path), "%s/assets/worldmap_mask.pcx", root);
    mask = W_LoadImage(path);
    snprintf(path, sizeof(path), "%s/assets/worldmap.txt", root);
    FILE *f = fopen(path, "r");
    if (!base || !mask || !f || base->w != mask->w || base->h != mask->h) {
        if (f) fclose(f);
        fprintf(stderr, "rustfront: the world map in %s/assets is incomplete; the campaign shows a list\n", root);
        return false;
    }
    memset(mission_at, -1, sizeof(mission_at));
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int id, x, y, n, used, r, g, b, amount;
        char word[32], faction[32];
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "territory %d %d %d %31s %n", &id, &x, &y, word, &used) == 4 && id >= 0 &&
            id < MAX_TERRITORIES) {
            territory_t *t = &territories[id];
            snprintf(t->name, sizeof(t->name), "%s", line + used);
            t->seat = (ivec2_t){x, y};
            t->holder = holder_index(word);
            if (id + 1 > territory_count) territory_count = id + 1;
        } else if (sscanf(line, "mission %31s %d %d", faction, &n, &id) == 3) {
            int fi = holder_index(faction) - 1;
            if (fi >= 0 && n >= 1 && n <= RF_CAMPAIGN_MISSIONS && id >= 0 && id < MAX_TERRITORIES)
                mission_at[fi][n - 1] = id;
        } else if (sscanf(line, "tint %31s %d %d %d %d", word, &r, &g, &b, &amount) == 5) {
            int h = holder_index(word);
            if (h > 0) tint_rgb[h] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b, tint_amount[h] = amount;
        }
    }
    fclose(f);
    loaded = territory_count > 0;
    return loaded;
}

void RF_FreeWorldMap(void) {
    if (!loaded) return;
    R_FreeSprite(&sheet);
    SDL_FreeSurface(base);
    SDL_FreeSurface(mask);
    free(layer);
    layer = NULL, base = mask = NULL, layer_key = -1, loaded = false;
}

bool RF_HaveWorldMap(void) { return loaded; }
const spritesheet_t *RF_WorldMapSheet(void) { return loaded ? &sheet : NULL; }
int RF_TerritoryCount(void) { return loaded ? territory_count : 0; }
const char *RF_TerritoryName(int t) { return t >= 0 && t < territory_count ? territories[t].name : ""; }
ivec2_t RF_TerritorySeat(int t) { return t >= 0 && t < territory_count ? territories[t].seat : (ivec2_t){0, 0}; }

int RF_MissionTerritory(int faction, int index) {
    return loaded && faction >= 0 && faction < RF_FACTION_COUNT && index >= 0 && index < RF_CAMPAIGN_MISSIONS
               ? mission_at[faction][index] : -1;
}

/* Who holds a territory in a faction's campaign as it stands: its holder at the start, then the
 * campaign's faction wherever it has won; the whole map once the campaign is won. */
int RF_TerritoryHolder(int faction, int t) {
    if (t < 0 || t >= territory_count) return 0;
    int won = RF_CampaignWon(faction);
    if (won >= RF_CAMPAIGN_MISSIONS) return faction + 1;
    for (int i = 0; i < won; ++i)
        if (mission_at[faction][i] == t) return faction + 1;
    return territories[t].holder;
}

int RF_TerritoryAt(ivec2_t at) {
    if (!loaded || at.x < 0 || at.y < 0 || at.x >= mask->w || at.y >= mask->h) return -1;
    uint8_t v = ((const uint8_t *)mask->pixels)[at.y * mask->pitch + at.x];
    return v < territory_count ? v : -1;
}

static uint8_t mask_at(int x, int y) {
    return ((const uint8_t *)mask->pixels)[y * mask->pitch + x];
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, int percent) {
    uint32_t out = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        int ca = (int)(a >> shift & 255), cb = (int)(b >> shift & 255);
        out |= (uint32_t)(ca + (cb - ca) * percent / 100) << shift;
    }
    return out;
}

/* The tinted territories and their borders, at the map's size, in the screen's palette. */
static void build_layer(int faction, int selected, const uint32_t *palette) {
    int key = faction * 1000 + RF_CampaignWon(faction) * 100 + (selected + 1);
    if (layer && layer_key == key && layer_palette == palette) return;
    int w = mask->w, h = mask->h;
    if (!layer) layer = malloc((size_t)w * h);
    if (!layer) return;
    uint8_t lut[HOLDERS][256];
    for (int o = 1; o < HOLDERS; ++o)
        for (int i = 0; i < 256; ++i)
            lut[o][i] = V_NearestIndex(mix_rgb(palette[i], tint_rgb[o], tint_amount[o]));
    int holder[MAX_TERRITORIES];
    for (int t = 0; t < territory_count; ++t) holder[t] = RF_TerritoryHolder(faction, t);
    uint8_t border = V_NearestIndex(0xff1a1410u), lit = V_NearestIndex(0xfffff0b4u);
    const uint8_t *pixels = base->pixels;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t t = mask_at(x, y), out = 0;
            if (t < territory_count && holder[t]) out = lut[holder[t]][pixels[y * base->pitch + x]];
            if (t < territory_count) {
                uint8_t r = x + 1 < w ? mask_at(x + 1, y) : t, d = y + 1 < h ? mask_at(x, y + 1) : t;
                if (r != t || d != t)
                    out = (t == selected || r == selected || d == selected) ? lit : border;
            }
            layer[y * w + x] = out;
        }
    layer_key = key;
    layer_palette = palette;
}

static void disc(ivec2_t c, int r, uint8_t ink) {
    for (int dy = -r; dy <= r; ++dy) {
        int half = (int)sqrtf((float)(r * r - dy * dy));
        V_FillRect((irect_t){c.x - half, c.y + dy, 2 * half + 1, 1}, ink);
    }
}

static void ring(ivec2_t c, int r, int width, uint8_t ink) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 > (r - width) * (r - width)) V_FillRect((irect_t){c.x + dx, c.y + dy, 1, 1}, ink);
        }
}

/* The map's left part (columns 0..view.w of the page) drawn into `screen` over the painted
 * background: holders' tints, borders, the selected territory lit, and the campaign's mission
 * markers (won: the faction's colour; the next: pulsing amber; locked: dark), numbered, with
 * their territories' names. */
void RF_DrawWorldMap(irect_t view, irect_t screen, int faction, int selected_mission, const bitmapfont_t *font,
                     uint32_t ticks_ms) {
    if (!loaded || view.w <= 0 || view.h <= 0 || screen.w <= 0) return;
    const uint32_t *palette = vpalette;
    int selected = RF_MissionTerritory(faction, selected_mission);
    build_layer(faction, selected, palette);
    if (!layer) return;
    int s = screen.w / view.w > 0 ? screen.w / view.w : 1;
    for (int y = view.y; y < view.y + view.h && y < mask->h; ++y)
        for (int x = view.x; x < view.x + view.w && x < mask->w; ++x) {
            uint8_t v = layer[y * mask->w + x];
            if (v) V_FillRect((irect_t){screen.x + (x - view.x) * s, screen.y + (y - view.y) * s, s, s}, v);
        }
    int won = RF_CampaignWon(faction);
    uint8_t dark = V_NearestIndex(0xff201812u), ink = V_NearestIndex(0xffdce6dcu);
    uint8_t own = V_NearestIndex(tint_rgb[faction + 1]), amber = V_NearestIndex(0xffffc460u),
            dim = V_NearestIndex(0xff5a5650u), white = V_NearestIndex(0xffffffffu);
    bool pulse = (ticks_ms / 400) % 2 == 0;
    uint8_t text_remap[256];
    if (font) V_ModulateRemap(text_remap, font->sprite.source_palette, 0xffdce6dcu);
    for (int i = 0; i < RF_CAMPAIGN_MISSIONS; ++i) {
        int t = RF_MissionTerritory(faction, i);
        if (t < 0) continue;
        ivec2_t seat = RF_TerritorySeat(t);
        ivec2_t c = {screen.x + (seat.x - view.x) * s, screen.y + (seat.y - view.y) * s};
        bool open = RF_MissionUnlocked(faction, i), done = i < won;
        disc(c, 8 * s, dark);
        disc(c, 7 * s, done ? own : open ? (pulse ? amber : V_NearestIndex(0xffc8862cu)) : dim);
        if (i == selected_mission) ring(c, 11 * s, s, white);
        char number[4];
        snprintf(number, sizeof(number), "%d", i + 1);
        V_DrawSmallText((irect_t){c.x - 3 * s, c.y - 3 * s, 7 * s, 7 * s}, number, done || open ? 0xff141210u : 0xffb4aaa0u,
                        (isize2_t){V_DrawSize().w / s, V_DrawSize().h / s});
        if (font) {
            const char *name = RF_TerritoryName(t);
            int tw = V_TextWidth(font, name) * s;
            ivec2_t at = {c.x - tw / 2, c.y + 10 * s};
            /* a dark shadow under the name so it reads on any ground */
            uint8_t shadow[256];
            V_ModulateRemap(shadow, font->sprite.source_palette, 0xff0c0a08u);
            V_DrawText((ivec2_t){at.x + s, at.y + s}, font, name, shadow);
            V_DrawText(at, font, name, i == selected_mission ? NULL : text_remap);
        }
    }
    (void)ink;
}
