#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "video lifecycle", #c)

static uint32_t pixel(SDL_Renderer *renderer) {
    uint32_t value = 0;
    SDL_Rect area = {0, 0, 1, 1};
    if (SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_ARGB8888, &value, sizeof(value)))
        return 0;
    return value;
}

int main(void) {
    app_t app = {.win = {64, 64}};
    CHECK(I_InitGraphics(&app, 64, 64, true, true));
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(palette);
    memset(screens[0].pixels, 77, (size_t)screens[0].w * screens[0].h);
    I_FinishUpdate();
    CHECK(pixel(app.renderer) == palette[77]);

    SDL_Event event = {.type = SDL_APP_WILLENTERBACKGROUND};
    I_GraphicsEvent(&event);
    memset(screens[0].pixels, 88, (size_t)screens[0].w * screens[0].h);
    I_FinishUpdate();
    CHECK(pixel(app.renderer) == palette[77]);
    event.type = SDL_RENDER_DEVICE_RESET;
    I_GraphicsEvent(&event);
    event.type = SDL_APP_DIDENTERFOREGROUND;
    I_GraphicsEvent(&event);
    I_FinishUpdate();
    CHECK(pixel(app.renderer) == palette[88]);
    // A second reset must also recreate the texture from the indexed framebuffer.
    event.type = SDL_RENDER_DEVICE_RESET;
    I_GraphicsEvent(&event);
    I_FinishUpdate();
    CHECK(pixel(app.renderer) == palette[88]);
    I_ShutdownGraphics();
    puts("PASS: background blocks presentation; device reset preserves indexed pixels");
    return 0;
}
