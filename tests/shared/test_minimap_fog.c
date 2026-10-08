#include "t_local.h"
#define CHECK(c) RTS_CHECK(c,"shared minimap fog",#c)
int main(void) {
    consoleplayer=0;
    uint32_t visibility[3]={0,SIGHT_EXPLORED,SIGHT_EXPLORED|0x40000000};
    level_t map={.width=3,.height=1,.sight={.cells=visibility,.allies={0x40000000}}};
    uint32_t colors[256];for(int i=0;i<256;i++)colors[i]=0xff000000u|(uint32_t)i*0x10101u;
    V_AllocScreen(5,3);I_SetPalette(colors);V_FillRect((irect_t){0,0,5,3},200);
    R_DrawMinimapFog(&map,(irect_t){1,1,3,1});
    CHECK(screens[0].pixels[6]==0);
    CHECK(screens[0].pixels[7]>0&&screens[0].pixels[7]<200);
    CHECK(screens[0].pixels[8]==200&&screens[0].pixels[5]==200&&screens[0].pixels[9]==200);
    V_SetClip((irect_t){2,1,1,1});
    V_FillRect((irect_t){2,1,1,1},200);
    R_DrawMinimapFog(&map,(irect_t){-1,0,6,3});
    CHECK(screens[0].pixels[8]==200);
    V_AllocScreen(10,6);V_SetDrawScale(2);V_SetClip((irect_t){0});
    V_FillRect((irect_t){0,0,5,3},200);
    R_DrawMinimapFog(&map,(irect_t){1,1,3,1});
    for(int y=2;y<4;y++)for(int x=2;x<8;x++) {
        int color=screens[0].pixels[y*10+x];
        CHECK(x<4?color==0:x<6?color>0&&color<200:color==200);
    }
    CHECK(screens[0].pixels[12]==200&&screens[0].pixels[28]==200);
    V_SetDrawScale(1);
    V_FreeScreen();puts("PASS: minimap shroud, explored dimming, visible terrain, bounds and clipping");return 0;
}
