#include "t_local.h"
#include "starcraft.h"
#include "info.h"
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static const char *root="data/STARCRAFT";
static int font_colors(const bitmapfont_t *font,const char *path) {
    spritesheet_t ramp={0};CHECK(font&&W_LoadIndexedSheet(path,&ramp));
    CHECK(font->sprite.palette_map_count==5);
    for(int row=0;row<5;row++)for(int ink=0;ink<8;ink++)
        CHECK(font->sprite.source_palette[row*8+ink+1]==ramp.palette[ramp.lumps[0].indices[row*8+ink]]);
    R_FreeSprite(&ramp);return 0;
}
static bool save(const char *path) {
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,screens[0].w,screens[0].h,32,SDL_PIXELFORMAT_ARGB8888);
    if(!s)return false;
    for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++)((uint32_t*)((uint8_t*)s->pixels+y*s->pitch))[x]=vpalette[screens[0].pixels[y*s->w+x]];
    SDL_Surface *rgb=SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_RGB24,0);
    bool ok=rgb&&SDL_SaveBMP(rgb,path)==0;SDL_FreeSurface(rgb);SDL_FreeSurface(s);return ok;
}
static int native_picture(const spritesheet_t *sheet,int frame,ivec2_t at,bool rim) {
    irect_t r=sheet->cells[frame].rect;
    const uint8_t *pixels=sheet->lumps[frame].indices;
    int visible=0;
    for(int y=0;y<r.h;y++)for(int x=0;x<r.w;x++) {
        if(rim&&y!=0&&y!=r.h-1)continue; /* Text overlays the interior. */
        unsigned index=pixels[y*r.w+x];
        if(!index)continue;
        CHECK(vpalette[screens[0].pixels[(at.y+y)*screens[0].w+at.x+x]]==sheet->palette[index]);
        visible++;
    }
    CHECK(visible>0);return 0;
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
    CHECK(W_ReadFile("data/STARCRAFT/native/rez/gluchat.bin",&b));
    CHECK(sc_decode_dialog(&b,&d)&&d.count==79);W_FreeFile(&b);
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
    CHECK(R_InitSprites(root,&level,units.items,units.count,&cache));CHECK(cache.count>228);
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
    fixed2_t before=fixed3_xy(unit->core.position),goal=fixed2_add(before,FIXED2_LIT(2,2));
    CHECK(P_MoveUnitTo(&level,unit,goal));for(int i=0;i<150;i++)P_Ticker();
    fixed2_t after=fixed3_xy(unit->core.position);CHECK(after.x>before.x+FIXED_ONE&&after.y>before.y+FIXED_ONE);
    P_UpdateSight();CHECK(P_SightBrightness(&level,(ivec2_t){127,127})==16);
    app_t app={.win={640,480},.cell={32,32},.running=true};V_AllocScreen(640,480);
    CHECK(M_Init(&app,root));
    /* The title (titledlg over title.pcx) shows once, then the main menu. */
    menu_t *front=G_ControlPanel(&app,false);CHECK(front&&M_MenuFind(front,-5)&&front->numitems!=10);
    M_SetupNextMenu(front);M_MenuDrawer(front);CHECK(save("/private/tmp/starcraft-title.bmp"));
    front->escape(front);front=currentmenu;CHECK(front&&front->numitems==10);
    CHECK(G_ControlPanel(&app,false)==front);M_MenuDrawer(front);CHECK(save("/private/tmp/starcraft-main-menu.bmp"));
    menuitem_t *start=M_MenuFind(front,3);CHECK(start&&start->routine);
    start->routine(front,start,MA_ACTIVATE);CHECK(menuactive&&!menumap&&currentmenu!=front);
    CHECK(!font_colors(M_MenuFind(currentmenu,8)->font,"data/STARCRAFT/native/glue/palnl/tfont.pcx"));
    CHECK(M_MenuFind(currentmenu,-1)&&M_MenuFind(currentmenu,-2)&&M_MenuFind(currentmenu,-3));
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-registry.bmp"));
    menuitem_t *next=M_MenuFind(currentmenu,6);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,3)->kind==MI_TEXTFIELD);
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-new-player.bmp"));
    next=M_MenuFind(currentmenu,1);next->routine(currentmenu,next,MA_ACTIVATE);
    next=M_MenuFind(currentmenu,4);next->routine(currentmenu,next,MA_ACTIVATE);
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-campaign.bmp"));
    CHECK(!font_colors(M_MenuFind(currentmenu,7)->font,"data/STARCRAFT/install/glue/palcs/tfont.pcx"));
    {
        menu_t *campaign=currentmenu;
        /* Load Saved: gluload's list, with Ok and Delete off until a row is chosen. */
        next=M_MenuFind(campaign,5);next->routine(campaign,next,MA_ACTIVATE);
        CHECK(currentmenu!=campaign&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Save Games"));
        CHECK(M_MenuFind(currentmenu,6)->kind==MI_LIST);
        if(!M_MenuFind(currentmenu,6)->rows)CHECK(!M_MenuFind(currentmenu,4)->enabled&&!M_MenuFind(currentmenu,7)->enabled);
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-load.bmp"));
        currentmenu->escape(currentmenu);CHECK(currentmenu==campaign);
        /* Play Custom: glucustm with the player, computer slots and races. */
        next=M_MenuFind(campaign,10);next->routine(campaign,next,MA_ACTIVATE);
        menu_t *custom=currentmenu;
        CHECK(custom!=campaign&&!strcmp(M_MenuFind(custom,65535)->text,"Create"));
        menuitem_t *maps=M_MenuFind(custom,5);CHECK(maps->kind==MI_LIST&&maps->rows>0&&maps->value==0);
        int road=-1;for(int i=0;i<maps->rows;i++)if(!strcmp(maps->row(maps,i),"(2)road war.scm"))road=i;
        CHECK(road>=0);
        custom->itemOn=(int)(maps-custom->items);
        SDL_Event key={.type=SDL_KEYDOWN};key.key.keysym.sym=SDLK_DOWN;
        for(int i=0;i<road;i++)CHECK(M_MenuResponder(custom,&app,&key));
        CHECK(maps->value==road&&!strcmp(M_MenuFind(custom,7)->text,"Road War"));
        CHECK(!strcmp(M_MenuFind(custom,37)->text,"Computer Slots:")&&!strcmp(M_MenuFind(custom,137)->text,"1"));
        CHECK(M_MenuFind(custom,20)->visible&&M_MenuFind(custom,20)->kind==MI_STATIC);
        CHECK(M_MenuFind(custom,21)->visible&&M_MenuFind(custom,21)->kind==MI_DROPDOWN);
        CHECK(!strcmp(M_MenuFind(custom,21)->text,"Computer")&&!M_MenuFind(custom,22)->visible);
        CHECK(M_MenuFind(custom,29)->visible&&M_MenuFind(custom,29)->rows==4);
        M_MenuDrawer(custom);CHECK(save("/private/tmp/starcraft-custom.bmp"));
        /* Closing the only computer is refused in a native glupok popup. */
        menuitem_t *seat=M_MenuFind(custom,21);seat->value=1;seat->routine(custom,seat,MA_CHANGE);
        CHECK(!M_MenuFind(custom,29)->visible);
        next=M_MenuFind(custom,12);next->routine(custom,next,MA_ACTIVATE);
        CHECK(currentmenu!=custom&&!menumap&&M_MenuFind(currentmenu,2)->prose);
        CHECK(strstr(M_MenuFind(currentmenu,2)->prose,"computer opponent"));
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-popup.bmp"));
        next=M_MenuFind(currentmenu,1);next->routine(currentmenu,next,MA_ACTIVATE);CHECK(currentmenu==custom);
        seat->value=0;seat->routine(custom,seat,MA_CHANGE);
        next=M_MenuFind(custom,12);next->routine(custom,next,MA_ACTIVATE);
        CHECK(!menuactive&&menumap&&strstr(menumap,"road war"));
        menumap=NULL;M_SetupNextMenu(campaign);
        /* Each race has its own briefing screen and palette; Start's planet
         * no longer reaches over Cancel. */
        next=M_MenuFind(campaign,8);next->routine(campaign,next,MA_ACTIVATE);
        CHECK(currentmenu!=campaign&&M_MenuFind(currentmenu,1));
        CHECK(!font_colors(M_MenuFind(currentmenu,65525)->font,"data/STARCRAFT/native/glue/palrz/tfont.pcx"));
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-briefing-zerg.bmp"));
        currentmenu->escape(currentmenu);CHECK(currentmenu==campaign);
    }
    next=M_MenuFind(currentmenu,7);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65525)->prose&&strstr(M_MenuFind(currentmenu,65525)->prose,"Train 10 Marines"));
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-briefing.bmp"));
    {
        menuitem_t *start=M_MenuFind(currentmenu,13),*cancel=M_MenuFind(currentmenu,14);
        SDL_Event press={.type=SDL_MOUSEBUTTONDOWN};press.button.button=SDL_BUTTON_LEFT;
        press.button.x=cancel->rect.x+cancel->rect.w/2;press.button.y=cancel->rect.y+2;
        CHECK(M_MenuResponder(currentmenu,&app,&press)&&currentmenu->held==cancel);
        press.button.x=start->rect.x+start->hitbox.x+start->hitbox.w-1;press.button.y=start->rect.y+start->hitbox.y+start->hitbox.h-1;
        CHECK(start->hitbox.w==185&&start->hitbox.h==122);
        CHECK(M_MenuResponder(currentmenu,&app,&press)&&currentmenu->held==start);
        currentmenu->held=NULL;
    }
    next=M_MenuFind(currentmenu,13);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(menumap&&!strcmp(menumap,g_game_default_map)&&!menuactive);
    front=G_ControlPanel(&app,false);CHECK(front&&front->numitems==10);
    next=M_MenuFind(front,4);CHECK(next&&next->routine);next->routine(front,next,MA_ACTIVATE);
    CHECK(currentmenu&&currentmenu!=front);
    CHECK(M_MenuFind(currentmenu,65535)&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Select Connection"));
    for(int i=0;i<currentmenu->numitems;i++)CHECK(!strstr(currentmenu->items[i].text,"Join Game"));
    next=M_MenuFind(currentmenu,5);CHECK(next&&next->kind==MI_LIST&&next->rows==1&&next->row);
    CHECK(!strcmp(next->row(next,0),"IPX network"));
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-connection.bmp"));
    next=M_MenuFind(currentmenu,9);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65535)&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Games"));
    CHECK(M_MenuFind(currentmenu,15)&&strstr(M_MenuFind(currentmenu,15)->text,"Create Game"));
    /* Retail Join lists hosted games only; 9 and 12 are text, not seat or speed choices. */
    next=M_MenuFind(currentmenu,5);CHECK(next&&next->rows==0&&next->row&&next->prose);
    CHECK(M_MenuFind(currentmenu,9)->kind==MI_STATIC&&M_MenuFind(currentmenu,12)->kind==MI_STATIC);
    CHECK(M_MenuFind(currentmenu,15)->enabled&&!M_MenuFind(currentmenu,13)->enabled);
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-join.bmp"));
    CHECK(!M_MenuFind(currentmenu,-1)->visible&&!M_MenuFind(currentmenu,-3)->visible);
    next=M_MenuFind(currentmenu,15);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65535)&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Create"));
    M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-create.bmp"));
    next=M_MenuFind(currentmenu,5);CHECK(next&&next->kind==MI_LIST);
    CHECK(next->rows==57&&next->row&&!next->prose);
    CHECK(next->row_height==next->font->line_h&&next->rect.h/next->row_height==6);
    CHECK(ivec2_equal(next->inset,(ivec2_t){0,0}));
    {
        menuitem_t *bar=M_MenuFind(currentmenu,-1),*up=M_MenuFind(currentmenu,-2),*down=M_MenuFind(currentmenu,-3);
        CHECK(bar&&bar->kind==MI_SCROLLBAR&&bar->sheet&&bar->thumb.cell==28);
        CHECK(bar->link==next-currentmenu->items&&bar->rect.x==next->rect.x+next->rect.w);
        CHECK(up&&down&&up->step==-1&&down->step==1&&up->link==bar->link&&down->link==bar->link);
        CHECK(!native_picture(bar->sheet,17,(ivec2_t){up->rect.x,up->rect.y},false));
        CHECK(!native_picture(bar->sheet,20,(ivec2_t){down->rect.x,down->rect.y},false));
        CHECK(bar->visible&&down->visible);
        menuitem_t *type=M_MenuFind(currentmenu,17);
        CHECK(!native_picture(bar->sheet,53,(ivec2_t){type->rect.x,type->rect.y+(type->rect.h-16)/2},true));
        CHECK(!native_picture(bar->sheet,50,(ivec2_t){type->rect.x+type->rect.w-13-5,type->rect.y+(type->rect.h-7)/2},false));
        SDL_Event mouse={.type=SDL_MOUSEBUTTONDOWN};mouse.button.button=SDL_BUTTON_LEFT;
        mouse.button.x=down->rect.x+down->rect.w/2;mouse.button.y=down->rect.y+down->rect.h/2;
        CHECK(M_MenuResponder(currentmenu,&app,&mouse)&&currentmenu->held==down);
        M_MenuDrawer(currentmenu);
        CHECK(!native_picture(bar->sheet,21,(ivec2_t){down->rect.x,down->rect.y},false));
        mouse.type=SDL_MOUSEBUTTONUP;CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        CHECK(next->first_row==1);
        mouse.button.y=up->rect.y+up->rect.h/2;
        mouse.type=SDL_MOUSEBUTTONDOWN;CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        mouse.type=SDL_MOUSEBUTTONUP;CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        CHECK(next->first_row==0);
        mouse.button.y=bar->rect.y;
        mouse.type=SDL_MOUSEBUTTONDOWN;CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        CHECK(currentmenu->held==bar&&next->first_row==0);
        mouse=(SDL_Event){.type=SDL_MOUSEMOTION};
        mouse.motion.x=bar->rect.x+bar->rect.w/2;mouse.motion.y=bar->rect.y+bar->rect.h-1;
        CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        mouse=(SDL_Event){.type=SDL_MOUSEBUTTONUP};mouse.button.button=SDL_BUTTON_LEFT;
        mouse.button.x=bar->rect.x+bar->rect.w/2;mouse.button.y=bar->rect.y+bar->rect.h-1;
        mouse.type=SDL_MOUSEBUTTONUP;CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        CHECK(next->first_row==next->rows-next->rect.h/next->row_height&&next->value==0);
        currentmenu->itemOn=(int)(next-currentmenu->items);
        SDL_Event home={.type=SDL_KEYDOWN};home.key.keysym.sym=SDLK_HOME;
        CHECK(M_MenuResponder(currentmenu,&app,&home)&&next->first_row==0);
        int road=-1;
        for(int i=0;i<next->rows;i++) {
            const char *name=next->row(next,i);CHECK(name&&name[0]);
            CHECK(!strstr(name,"enslavers"));
            if(!strcmp(name,"(2)road war.scm"))road=i;
        }
        CHECK(road>=0);
        currentmenu->itemOn=(int)(next-currentmenu->items);
        SDL_Event arrow={.type=SDL_KEYDOWN};arrow.key.keysym.sym=SDLK_DOWN;
        for(int i=0;i<road;i++)CHECK(M_MenuResponder(currentmenu,&app,&arrow));
        CHECK(next->value==road&&road>=next->first_row&&road<next->first_row+next->rect.h/next->row_height);
        CHECK(!strcmp(M_MenuFind(currentmenu,7)->text,"Road War"));
        CHECK(!strcmp(M_MenuFind(currentmenu,10)->text,"Map Size:")&&!strcmp(M_MenuFind(currentmenu,110)->text,"128x128"));
        CHECK(!strcmp(M_MenuFind(currentmenu,9)->text,"Number of Players:")&&!strcmp(M_MenuFind(currentmenu,109)->text,"2"));
        CHECK(M_MenuFind(currentmenu,110)->align==MALIGN_RIGHT);
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-create-road-war.bmp"));
        app.win=(isize2_t){1280,960};V_AllocScreen(1280,960);
        V_BeginFrame(0xff000000);
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-create-road-war-scaled.bmp"));
        app.win=(isize2_t){640,480};V_AllocScreen(640,480);
        V_BeginFrame(0xff000000);
        mouse=(SDL_Event){.type=SDL_MOUSEMOTION};
        mouse.motion.x=type->rect.x+type->rect.w-10;mouse.motion.y=type->rect.y+type->rect.h/2;
        CHECK(M_MenuResponder(currentmenu,&app,&mouse));
        M_MenuDrawer(currentmenu);
        CHECK(!native_picture(bar->sheet,56,(ivec2_t){type->rect.x,type->rect.y+(type->rect.h-16)/2},true));
        CHECK(!native_picture(bar->sheet,51,(ivec2_t){type->rect.x+type->rect.w-13-5,type->rect.y+(type->rect.h-7)/2},false));
        mouse.type=SDL_MOUSEBUTTONDOWN;mouse.button.button=SDL_BUTTON_LEFT;
        mouse.button.x=type->rect.x+type->rect.w-10;mouse.button.y=type->rect.y+type->rect.h/2;
        CHECK(M_MenuResponder(currentmenu,&app,&mouse)&&currentmenu->dropdown==type);
        /* The open combobox squares off toward its list; the list sits on dlg.grp 35..43. */
        M_MenuDrawer(currentmenu);
        CHECK(!native_picture(bar->sheet,47,(ivec2_t){type->rect.x,type->rect.y+(type->rect.h-16)/2},true));
        CHECK(!native_picture(bar->sheet,41,(ivec2_t){type->rect.x,type->rect.y+type->rect.h-2+32-16},true));
        home.key.keysym.sym=SDLK_RETURN;CHECK(M_MenuResponder(currentmenu,&app,&home)&&!currentmenu->dropdown);
        menuitem_t *ok=M_MenuFind(currentmenu,12);CHECK(ok&&ok->enabled&&ok->routine);
        ok->routine(currentmenu,ok,MA_ACTIVATE);
        CHECK(M_NetHosting()&&doomcom->numplayers==2);
        CHECK(!strcmp(M_MenuFind(currentmenu,14)->text,"Road War"));
        CHECK(M_MenuFind(currentmenu,29)->kind==MI_DROPDOWN);
        CHECK(ivec2_equal(M_MenuFind(currentmenu,29)->inset,(ivec2_t){4,0}));
        CHECK(!strcmp(M_MenuFind(currentmenu,6)->text,"Ready")&&!M_MenuFind(currentmenu,8)->text[0]);
        CHECK(M_MenuFind(currentmenu,28)->kind==MI_STATIC&&M_MenuFind(currentmenu,32)->kind==MI_DROPDOWN);
        next=M_MenuFind(currentmenu,6);next->routine(currentmenu,next,MA_ACTIVATE);
        CHECK(!strcmp(next->text,"Unready")&&!M_MenuFind(currentmenu,29)->enabled);
        next->routine(currentmenu,next,MA_ACTIVATE);
        CHECK(!strcmp(next->text,"Ready")&&M_MenuFind(currentmenu,29)->enabled);
        M_MenuDrawer(currentmenu);CHECK(save("/private/tmp/starcraft-lobby.bmp"));
    }
    next=M_MenuFind(currentmenu,7);CHECK(next&&next->routine);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65535)&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Games"));
    next=M_MenuFind(currentmenu,14);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(M_MenuFind(currentmenu,65535)&&!strcmp(M_MenuFind(currentmenu,65535)->text,"Select Connection"));
    next=M_MenuFind(currentmenu,10);next->routine(currentmenu,next,MA_ACTIVATE);
    CHECK(currentmenu==front&&M_MenuFind(currentmenu,3)&&!strcmp(M_MenuFind(currentmenu,3)->text,"Single Player"));
    M_NetStop();M_ClearMenus();
    CHECK(!menuactive&&menumap&&!strcmp(menumap,g_game_default_map));
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
