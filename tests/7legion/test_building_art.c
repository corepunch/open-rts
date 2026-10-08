#include "t_local.h"
#include "info.h"
#define CHECK(c) RTS_CHECK(c, "7legion buildings", #c)

int main(void) {
    G_InitGame();
    CHECK(G_DoLoadLevel("data/7LEGION/DATA/MAPT.000", &level));
    spritecache_t cache = {0};
    int count = 0;
    V_AllocScreen(640, 480);
    for (int i = 0; i < num_actor_types; ++i) {
        const mobjtype_t *type = &actor_types[i];
        if (type->footprint.w == 0) continue;
        mobj_t image = {.type_id = type->id, .info = type};
        snprintf(image.core.sprite_name, sizeof(image.core.sprite_name), "%s", type->sprite_name);
        mobj_t *images[] = {&image};
        CHECK(R_InitSprites("data/7LEGION", &level, images, 1, &cache));
        const spritesheet_t *sprite = R_CacheLookup(&cache, type->sprite_name);
        CHECK(sprite && sprite->numlumps == 1);
        CHECK(sprite->frame_size.w == type->footprint.w * 32 && sprite->frame_size.h == type->footprint.h * 32);
        CHECK(sprite->cells[0].ground_point.x == 0 && sprite->cells[0].ground_point.y == 0);
        I_SetPalette(sprite->palette);
        irect_t dst = {(count % 3) * 210, (count / 3) * 160, sprite->frame_size.w, sprite->frame_size.h};
        R_DrawSprite(sprite, 0, -1, NULL, &dst, V_OPAQUE, 16);
        ++count;
    }
    CHECK(count == 9 && cache.count == 9);
    CHECK(R_BindSprites(&cache, gameinfo));
    /* Exact first tile of the base, from the native descriptor (tile 1). */
    blob_t bim;
    char path[1024];
    M_PathJoin(path, sizeof(path), "data/7LEGION", level.tileset_name);
    CHECK(W_ReadFile(path, &bim));
    const uint8_t *bytes = (const uint8_t *)bim.bytes;
    uint32_t offset = read_u32_le(bytes + 4);
    const spritesheet_t *base = R_CacheLookup(&cache, "bt_base");
    for (int row = 0; row < 32; ++row)
        CHECK(!memcmp(base->lumps[0].indices + row * base->frame_size.w, bytes + offset + 4 + row * 32, 32));
    W_FreeFile(&bim);
    if (getenv("OPEN_RTS_BUILDINGS_BMP")) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
        CHECK(surface);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                ((uint32_t *)((uint8_t *)surface->pixels + y * surface->pitch))[x] = vpalette[screens[0].pixels[y * 640 + x]];
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
        CHECK(rgb && SDL_SaveBMP(rgb, getenv("OPEN_RTS_BUILDINGS_BMP")) == 0);
        SDL_FreeSurface(rgb);
        SDL_FreeSurface(surface);
    }
    V_FreeScreen();
    R_FreeSpriteCache(&cache);
    P_FreeLevel(&level);
    puts("PASS: nine native building composites, footprint sizes, pivots and exact base tile pixels");
    return 0;
}
