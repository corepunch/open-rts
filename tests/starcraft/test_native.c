#include "t_local.h"
#include "starcraft.h"
#include "info.h"
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static const char *root="data/STARCRAFT";
static bool save(const char *path) {
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,screens[0].w,screens[0].h,32,SDL_PIXELFORMAT_ARGB8888);
    if(!s)return false;
    for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++)((uint32_t*)((uint8_t*)s->pixels+y*s->pitch))[x]=vpalette[screens[0].pixels[y*s->w+x]];
    SDL_Surface *rgb=SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_RGB24,0);
    bool ok=rgb&&SDL_SaveBMP(rgb,path)==0;SDL_FreeSurface(rgb);SDL_FreeSurface(s);return ok;
}
static int dialog_test(void) {
    blob_t b={0};sc_dialog_t d;
    CHECK(W_ReadFile("data/STARCRAFT/native/rez/glumain.bin",&b));
    CHECK(sc_decode_dialog(&b,&d)&&d.count==10);
    CHECK(d.controls[2].id==3&&!strcmp(d.controls[2].text,"Single Player"));
    CHECK(d.controls[2].rect.x==0&&d.controls[2].rect.y==0&&d.controls[2].text_offset.x==150);
    CHECK(d.controls[2].movie_count==2&&d.controls[2].movies[0].offset.x==46);
    CHECK(d.controls[2].hotkey=='s'&&d.controls[2].mark_len==1);
    size_t n=b.size;b.size=100;CHECK(!sc_decode_dialog(&b,&d));b.size=n;
    b.bytes[86]=86;b.bytes[87]=b.bytes[88]=b.bytes[89]=0;CHECK(!sc_decode_dialog(&b,&d));W_FreeFile(&b);
    const char *files[]={"statdata","statbtnt","statport","minimap","gamemenu","stat_f10","statres"};
    for(unsigned i=0;i<sizeof(files)/sizeof(*files);i++) {
        char path[256];snprintf(path,sizeof(path),"%s/native/rez/%s.bin",root,files[i]);
        CHECK(W_ReadFile(path,&b)&&sc_decode_dialog(&b,&d));
        if(i==1){CHECK(d.count==9);CHECK(d.controls[0].rect.x==505&&d.controls[0].rect.y==358);}
        if(i==3){CHECK(d.controls[0].rect.x==6&&d.controls[0].rect.y==348&&d.controls[0].rect.w==128);}
        W_FreeFile(&b);
    }
    return 0;
}
static int malformed_grp(void) {
    uint8_t bytes[]={1,0,4,0,3,0,2,1,2,1,14,0,0,0,2,0,0x42,7};
    blob_t b={.bytes=bytes,.size=sizeof(bytes)};uint32_t pal[256]={0};spritesheet_t s={0};
    CHECK(sc_decode_grp(&b,pal,false,&s));CHECK(s.lumps[0].indices[0]==7&&s.lumps[0].indices[1]==7);
    CHECK(ivec2_equal(s.cells[0].displacement,(ivec2_t){2,1}));
    CHECK(ivec2_equal(s.cells[0].ground_point,(ivec2_t){0,0}));R_FreeSprite(&s);
    b.size--;CHECK(!sc_decode_grp(&b,pal,false,&s));
    b.size=sizeof(bytes);bytes[16]=0;CHECK(!sc_decode_grp(&b,pal,false,&s));return 0;
}
int main(void) {
    CHECK(!dialog_test()&&!malformed_grp());
    CHECK(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO)==0);
    G_InitGame();P_InitThinkers();CHECK(G_DoLoadLevel("catalog",&level));CHECK(P_InitSight());
    CHECK(P_LoadThings("catalog")==228);mobjlist_t units=P_ListMobjs();CHECK(units.count==228);
    tileset_t tiles={0};spritesheet_t fallback={0};spritecache_t cache={0};
    CHECK(W_LoadAssets(root,&level,"sc-000",&tiles,&fallback));CHECK(tiles.count==4844);
    CHECK(R_InitSprites(root,&level,units.items,units.count,&cache));CHECK(cache.count==228);
    int unique=0;unsigned pictures=0;
    for(int i=0;i<228;i++) {
        char name[16];snprintf(name,sizeof(name),"sc-%03d",i);const spritesheet_t *s=R_CacheLookup(&cache,name);
        CHECK(s&&s->numlumps>0&&s->spritedef.numframes>0);
        if(!cache.entries[i].alias){unique++;pictures+=s->numlumps;CHECK(s->palette_map_count==8);}
        for(int f=0;f<s->spritedef.numframes;f++)for(int r=0;r<s->spritedef.spriteframes[f].rotations;r++) {
            const spritelayer_t *layer=s->spritedef.spriteframes[f].directions[r].layers;
            if(layer)CHECK(layer->lump<s->numlumps);
        }
        const state_t *state=&states[mobjinfo[i+1].spawnstate];CHECK(state->frame<s->spritedef.numframes);
    }
    CHECK(unique==148);
    const spritesheet_t *marine=R_CacheLookup(&cache,"sc-000");
    CHECK(marine->spritedef.spriteframes[0].rotations==32);
    const spritedirection_t *d=marine->spritedef.spriteframes[0].directions;
    CHECK(d[0].layers[0].lump==0&&d[16].layers[0].lump==16);
    CHECK(d[8].layers[0].lump==8&&d[24].layers[0].lump==8);
    CHECK(d[8].layers[0].flags&RTS_FRAME_FLIP_X);CHECK(!(d[24].layers[0].flags&RTS_FRAME_FLIP_X));
    mobj_t *unit=units.items[0];CHECK(unit->type_id==1);
    fvec2_t before=fixed3_xy_to_fvec2(unit->core.position),goal=fvec2_add(before,(fvec2_t){2,2});
    CHECK(P_MoveUnitTo(&level,unit,goal));for(int i=0;i<150;i++)P_Ticker();
    fvec2_t after=fixed3_xy_to_fvec2(unit->core.position);CHECK(after.x>before.x+1&&after.y>before.y+1);
    P_UpdateSight();CHECK(P_SightBrightness(&level,(ivec2_t){127,127})==16);
    app_t app={.win={640,480},.cell={32,32},.running=true};V_AllocScreen(640,480);
    CHECK(M_Init(&app,root));menu_t *front=G_ControlPanel(&app,false);CHECK(front&&front->numitems==10);
    M_StartControlPanel(&app);M_MenuDrawer(front);CHECK(save("/private/tmp/starcraft-main-menu.bmp"));
    menuitem_t *start=M_MenuFind(front,3);CHECK(start&&start->routine);
    start->routine(front,start,MA_ACTIVATE);CHECK(menuactive&&!menumap&&currentmenu!=front);
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-registry.bmp"));
    menuitem_t *next=M_MenuFind(currentmenu,6);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,3)->kind==MI_TEXTFIELD);
    next=M_MenuFind(currentmenu,1);next->routine(currentmenu,next,MA_ACTIVATE);
    next=M_MenuFind(currentmenu,4);next->routine(currentmenu,next,MA_ACTIVATE);
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-campaign.bmp"));
    next=M_MenuFind(currentmenu,7);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65525)->prose&&strstr(M_MenuFind(currentmenu,65525)->prose,"Train 10 Marines"));
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-briefing.bmp"));
    next=M_MenuFind(currentmenu,13);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(menumap&&!strcmp(menumap,g_game_default_map)&&!menuactive);
    menu_t *hud=G_InitHUD(&app,root);CHECK(hud);
    hudview=(hudview_t){.units=units.items,.unit_count=units.count,.sprites=&cache,.tileset=&tiles};
    P_MobjSetSelected(unit,true);M_CentreView(&app,fixed3_xy_to_fvec2(unit->core.position));
    SDL_Event click={.type=SDL_MOUSEBUTTONDOWN};click.button.button=SDL_BUTTON_LEFT;click.button.x=320;click.button.y=100;
    CHECK(!M_MenuResponder(hud,&app,&click));
    /* Exercise the real HUD/world dispatch, not only P_MoveUnitTo. */
    ivec2_t point={-1,-1};
    for(int y=0;y<340&&point.x<0;y+=2)for(int x=0;x<640&&point.x<0;x+=2)
        if(R_PickUnit(&app,&level,units.items,units.count,&fallback,&cache,gameinfo,x,y,consoleplayer)==0)
            point=(ivec2_t){x,y};
    CHECK(point.x>=0);P_MobjSetSelected(unit,false);
    click.button.x=point.x;click.button.y=point.y;
    CHECK(!M_MenuResponder(hud,&app,&click));
    G_Responder(&app,&level,units.items,units.count,&fallback,&cache,gameinfo,&click);
    click.type=SDL_MOUSEBUTTONUP;
    CHECK(!M_MenuResponder(hud,&app,&click));
    G_Responder(&app,&level,units.items,units.count,&fallback,&cache,gameinfo,&click);
    CHECK(P_MobjIsSelected(unit));
    click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_RIGHT;
    click.button.x=point.x+64;click.button.y=point.y;
    CHECK(!M_MenuResponder(hud,&app,&click));
    netactive=true;G_Responder(&app,&level,units.items,units.count,&fallback,&cache,gameinfo,&click);
    ticcmd_t order;G_BuildTiccmd(&order);netactive=false;
    CHECK(order.order==TC_ORDER&&order.count==1&&order.units[0]==unit->id);
    G_RunTiccmd(consoleplayer,&order);CHECK(P_HasMoveOrder(unit));
    V_BeginFrame(0xff000000);R_DrawLevel(&app,&level,&tiles);R_RenderPlayerView(&app,&level,&tiles,units.items,units.count,&fallback,&cache,gameinfo,0);
    M_MenuDrawer(hud);CHECK(save("/private/tmp/starcraft-selected-marine.bmp"));
    bool minimap=false,portrait=false,move=false;
    for(int i=0;i<hud->numitems;i++) {
        menuitem_t *it=&hud->items[i];
        if(it->kind==MI_MINIMAP){CHECK(it->rect.x==6&&it->rect.y==348&&it->rect.w==128);minimap=true;}
        if(it->rect.x==413&&it->rect.y==410){CHECK(it->visible&&it->ownerdraw);portrait=true;}
        if(it->rect.x==505&&it->rect.y==358){CHECK(it->visible&&it->sheet&&it->look[0].cell==228);move=true;}
    }
    CHECK(minimap&&portrait&&move);
    int cached=cache.count,state=unit->core.state_id;
    CHECK(R_InitSprites(root,&level,units.items,units.count,&cache));
    CHECK(cache.count==cached&&unit->core.state_id==state);
    menu_t *pause=G_ControlPanel(&app,true);CHECK(pause&&M_MenuFind(pause,65533));
    M_MenuDrawer(pause);CHECK(save("/private/tmp/starcraft-pause.bmp"));
    P_MobjSetSelected(unit,false);P_MobjSetSelected(units.items[106],true);
    M_CentreView(&app,fixed3_xy_to_fvec2(units.items[106]->core.position));
    V_BeginFrame(0xff000000);R_DrawLevel(&app,&level,&tiles);R_RenderPlayerView(&app,&level,&tiles,units.items,units.count,&fallback,&cache,gameinfo,0);
    M_MenuDrawer(hud);CHECK(save("/private/tmp/starcraft-buildings.bmp"));
    G_ShutdownHUD();M_Shutdown();R_FreeSpriteCache(&cache);R_FreeTileset(&tiles);P_FreeMobjList(&units);P_FreeLevel(&level);SDL_Quit();
    printf("PASS: 228 units, %d GRPs, %u native frames; malformed assets rejected; native dialogs, portraits, direction and movement verified\n",unique,pictures);
    return 0;
}
