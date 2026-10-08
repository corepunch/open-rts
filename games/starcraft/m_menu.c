#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { spritesheet_t sheet; unsigned ms; uint32_t *palettes; } movie_t;
typedef struct { sc_control_t native; spritesheet_t image; movie_t movies[SC_CONTROL_MOVIES]; } artwork_t;
static spritesheet_t background, console, wireframe, icons, widgets, panel;
static bitmapfont_t fonts[4], gamefonts[4];
static artwork_t art[SC_DIALOG_CONTROLS];
static menuitem_t items[SC_DIALOG_CONTROLS], pauseitems[SC_DIALOG_CONTROLS], huditems[160];
static menu_t front={.items=items,.size={640,480},.stretch=true,.modal=true};
static menu_t pausemenu={.items=pauseitems,.size={640,480},.stretch=true,.modal=true};
static menu_t hud={.items=huditems,.size={640,480},.stretch=true};
static movie_t portrait;
static char asset_root[1024];
static int portrait_item, portrait_id=-1;
static int selected_id=-1, selection_start, command_start, wire_item, name_item, hp_item;
static int catalog_index;
static void native_path(char *out,size_t size,const char *root,const char *name) {
    snprintf(out,size,"%s/native/%s",root,name);
    for(char *p=out+strlen(root)+1;*p;p++) { if(*p=='\\') *p='/'; *p=(char)tolower((unsigned char)*p); }
}
static menuitem_t control(const sc_control_t *c,bitmapfont_t *f) {
    bool button=c->type==1||c->type==2||c->type==14;
    menuitem_t item={.id=c->id,.kind=button?MI_BUTTON:MI_STATIC,.rect=c->rect,.flags=c->flags,
        .visible=(c->flags&8)!=0,.enabled=!(c->flags&2),.release=true,.inset=c->text_offset,
        .hotkey=c->hotkey<128?tolower(c->hotkey):0,.mark_at=c->mark_at,.mark_len=c->mark_len};
    int fi=c->flags&0x400?0:c->flags&0x10000?1:c->flags&0x800?2:c->flags&0x4000?3:0;
    item.font=&f[fi]; snprintf(item.text,sizeof(item.text),"%s",c->text);
    item.align=(c->flags&0xa00000)?MALIGN_HCENTER:(c->flags&0x400000)?MALIGN_RIGHT:0;
    if(c->flags&0x2000000) item.align|=MALIGN_VCENTER;
    if(c->flags&0x4000000) item.align|=MALIGN_BOTTOM;
    if(c->type==10) item.align=MALIGN_HCENTER;
    if(c->type==11) item.align=MALIGN_RIGHT;
    if(c->type==1||c->type==2) item.align=MALIGN_CENTER;
    for(int s=0;s<MS_STATES;s++) item.look[s]=(menulook_t){.palette=button?1:0,.cell=-1};
    item.look[MS_FOCUS].palette=2; item.look[MS_PUSHED].palette=2; item.look[MS_DISABLED].palette=3;
    return item;
}
static void begin(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE){menumap=g_game_default_map;M_ClearMenus();}
}
static void resume(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_ClearMenus();
}
static void leave(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE){menuleave=true;M_ClearMenus();}
}
static void unavailable(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_StartMessage("Choose Single Player to explore the first Terran map.\nOther menu actions are not implemented.");
}
static void help(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_StartMessage("WASD pans. Select a unit and right-click to move.\nUse --map catalog for all 228 native unit slots.\nEconomy, combat and mission triggers are not implemented.");
}
static void open_menu(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)i;if(a==MA_ACTIVATE)M_StartControlPanel(m->app);
}
static bool draw_front(const menu_t *menu,const menuitem_t *item,menustate_t state,irect_t rect) {
    (void)menu;
    const artwork_t *a=item->userdata;
    if(!a)return false;
    const sc_control_t *c=&a->native;
    /* Both normal and hover movies are overlays, not alternate full buttons. */
    for(int hover=0;hover<2;hover++) for(int j=0;j<c->movie_count;j++) {
        const sc_movie_ref_t *ref=&c->movies[j];
        if(((ref->flags&8)!=0)!=hover || (hover && state!=MS_FOCUS && state!=MS_PUSHED))continue;
        const movie_t *movie=&a->movies[j];
        unsigned frame=SDL_GetTicks()/movie->ms;
        if(ref->flags&4) frame%=movie->sheet.numlumps;
        else if(frame>=(unsigned)movie->sheet.numlumps)frame=movie->sheet.numlumps-1;
        irect_t dst={rect.x+ref->offset.x*rect.w/c->rect.w,rect.y+ref->offset.y*rect.h/c->rect.h,
                    movie->sheet.frame_size.w*rect.w/c->rect.w,movie->sheet.frame_size.h*rect.h/c->rect.h};
        R_DrawIndexed(movie->sheet.lumps[frame].indices,movie->sheet.frame_size,movie->palettes+frame*256,NULL,NULL,&dst,0);
    }
    return false;
}
static bool grp(const char *root,const char *name,const uint32_t *palette,spritesheet_t *sheet) {
    blob_t b={0}; if(!sc_read(root,name,&b))return false;
    bool ok=sc_decode_grp(&b,palette,false,sheet); W_FreeFile(&b); return ok;
}
static void draw_portrait(const menu_t *menu,const menuitem_t *item,irect_t rect) {
    (void)menu;(void)item;if(!portrait.sheet.numlumps)return;
    unsigned frame=SDL_GetTicks()/portrait.ms % portrait.sheet.numlumps;
    R_DrawIndexed(portrait.sheet.lumps[frame].indices,portrait.sheet.frame_size,portrait.palettes+frame*256,NULL,NULL,&rect,V_OPAQUE);
}
static bool draw_pause(const menu_t *menu,const menuitem_t *item,menustate_t state,irect_t rect) {
    int scale=rect.h/item->rect.h; if(scale<1)scale=1;
    if(item->id==-1) {
        int cell=panel.frame_size.w*scale;
        for(int y=0;y<rect.h;y+=cell)for(int x=0;x<rect.w;x+=cell) {
            int frame=(y==0?0:y+cell>=rect.h?6:3)+(x==0?0:x+cell>=rect.w?2:1);
            irect_t dst={rect.x+x,rect.y+y,cell,cell};
            R_DrawSprite(&panel,frame,-1,NULL,&dst,0,16);
        }
    } else if(item->kind==MI_BUTTON) {
        int frame=state==MS_PUSHED?118:state==MS_DISABLED?112:115;
        int lw=widgets.cells[frame].rect.w*scale,rw=widgets.cells[frame+2].rect.w*scale;
        irect_t dst={rect.x,rect.y,lw,rect.h};
        R_DrawSprite(&widgets,frame,-1,NULL,&dst,0,16);
        dst=(irect_t){rect.x+lw,rect.y,rect.w-lw-rw,rect.h};R_DrawSprite(&widgets,frame+1,-1,NULL,&dst,0,16);
        dst=(irect_t){rect.x+rect.w-rw,rect.y,rw,rect.h};R_DrawSprite(&widgets,frame+2,-1,NULL,&dst,0,16);
    }
    (void)menu;return false;
}

bool G_InitMenus(app_t *app,const char *root) {
    char path[2048];native_path(path,sizeof(path),root,"glue/palmm/backgnd.pcx");
    if(!W_LoadIndexedSheet(path,&background))return false;
    const char *names[]={"font10","font14","font16","font16x"};
    for(int i=0;i<4;i++) {
        if(!sc_font(root,names[i],&fonts[i])||!sc_font(root,names[i],&gamefonts[i]))return false;
        if(!sc_font_colors(root,"game/tfontgam.pcx",&gamefonts[i]))return false;
    }
    sc_dialog_t d;
    if(!sc_dialog(root,"rez/glumain.bin",&d))return false;
    front.app=app;front.numitems=d.count;front.background=&background;front.palette=background.palette;front.drawitem=draw_front;
    for(int i=0;i<d.count;i++) {
        sc_control_t *c=&d.controls[i]; artwork_t *a=&art[i]; a->native=*c;
        items[i]=control(c,fonts);items[i].userdata=a;
        items[i].routine=c->id==3?begin:c->id==2?M_MenuQuitGame:unavailable;
        if(c->type==5&&c->text[0]) {
            native_path(path,sizeof(path),root,c->text);
            if(!W_LoadIndexedSheet(path,&a->image))return false;
            items[i].sheet=&a->image;items[i].text[0]=0;
            for(int s=0;s<MS_STATES;s++)items[i].look[s].cell=0;
        }
        for(int j=0;j<c->movie_count;j++) {
            native_path(path,sizeof(path),root,c->movies[j].path);
            movie_t *v=&a->movies[j];
            if(!sc_movie(path,&v->sheet,&v->ms,&v->palettes))return false;
        }
    }
    if(!sc_dialog(root,"rez/gamemenu.bin",&d))return false;
    if(!grp(root,"dlgs/terran.grp",background.palette,&widgets)||!grp(root,"dlgs/tile.grp",background.palette,&panel))return false;
    pausemenu.app=app;pausemenu.numitems=d.count+1;pausemenu.drawitem=draw_pause;
    pauseitems[0]=(menuitem_t){.kind=MI_STATIC,.id=-1,.visible=true,.rect=d.rect};
    for(int i=0;i<d.count;i++) {
        pauseitems[i+1]=control(&d.controls[i],gamefonts);
        pauseitems[i+1].routine=pauseitems[i+1].id==65533?resume:pauseitems[i+1].id==6?leave:pauseitems[i+1].id==4?help:unavailable;
    }
    return true;
}
menu_t *G_ControlPanel(app_t *app,bool inlevel) {
    menu_t *menu=inlevel?&pausemenu:&front;menu->app=app;return menu;
}
void G_ShutdownMenus(void) {
    R_FreeSprite(&background);
    for(int i=0;i<4;i++){R_FreeSprite(&fonts[i].sprite);R_FreeSprite(&gamefonts[i].sprite);}
    for(int i=0;i<SC_DIALOG_CONTROLS;i++) {
        R_FreeSprite(&art[i].image);
        for(int j=0;j<SC_CONTROL_MOVIES;j++){R_FreeSprite(&art[i].movies[j].sheet);free(art[i].movies[j].palettes);}
    }
    memset(art,0,sizeof(art));
}
static void focus(menu_t *menu,menuitem_t *item,menuaction_t action) {
    if(action!=MA_ACTIVATE)return;
    catalog_index=(catalog_index+item->value+SC_TYPES)%SC_TYPES;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next)if(th->function==P_MobjThinker) {
        mobj_t *unit=(mobj_t*)th;bool chosen=unit->type_id==catalog_index+1;
        P_MobjSetSelected(unit,chosen);
        if(chosen)M_CentreView(menu->app,fixed3_xy_to_fvec2(unit->core.position));
    }
}
static void stop(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)HU_SelectedOrder(TC_STOP,(fvec2_t){0},0);
}
static void move(menu_t *m,menuitem_t *i,menuaction_t a) {
    if(a==MA_ACTIVATE)m->target=i;
    if(a!=MA_TARGET)return;
    cell_t at=R_ScreenToGrid(m->app,m->cursor.x,m->cursor.y);
    HU_SelectedOrder(TC_MOVE,(fvec2_t){at.x,at.y},0);
    m->target=NULL;
}
static void refresh(menu_t *menu) {
    (void)menu;mobj_t *selected=NULL;
    for(int i=0;i<hudview.unit_count;i++)if(P_MobjIsSelected(hudview.units[i])){selected=hudview.units[i];break;}
    selected_id=selected?selected->type_id-1:-1;
    for(int i=selection_start;i<command_start;i++)huditems[i].visible=false;
    for(int i=0;i<9;i++)huditems[command_start+i].visible=false;
    huditems[portrait_item].visible=false;
    if(!selected)return;
    catalog_index=selected_id;
    int id=sc_units[selected_id].portrait;
    if(id!=portrait_id) {
        R_FreeSprite(&portrait.sheet);free(portrait.palettes);memset(&portrait,0,sizeof(portrait));portrait_id=id;
        char path[2048];
        if(sc_portrait(asset_root,id,path,sizeof(path)))sc_movie(path,&portrait.sheet,&portrait.ms,&portrait.palettes);
    }
    huditems[portrait_item].visible=portrait.sheet.numlumps>0;
    huditems[name_item].visible=true;snprintf(huditems[name_item].text,128,"%s",sc_units[selected_id].name);
    huditems[hp_item].visible=true;snprintf(huditems[hp_item].text,128,"%d/%d",selected->hp,sc_units[selected_id].hp);
    huditems[wire_item].visible=selected_id<wireframe.numlumps;
    for(int s=0;s<MS_STATES;s++)huditems[wire_item].look[s].cell=selected_id;
    if(selected->traits&MF_MOBILE)huditems[command_start].visible=huditems[command_start+1].visible=true;
}
static void minimap(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    const tileset_t *tiles=hudview.tileset;
    if (!tiles || rect.w<1 || rect.h<1) return;
    for (int y=0;y<rect.h;y++) for (int x=0;x<rect.w;x++) {
        int tile=level.tile_ids[(y*level.height/rect.h)*level.width+x*level.width/rect.w];
        tile=tiles->tile_lookup[tile];
        V_DrawPoint(ivec2_add((ivec2_t){rect.x,rect.y},(ivec2_t){x,y}),tiles->indices[tile*1024+16*32+16]);
    }
    for (int i=0;i<hudview.unit_count;i++) {
        const mobj_t *u=hudview.units[i];
        if (u->remove) continue;
        fvec2_t at=fixed3_xy_to_fvec2(u->core.position);
        uint32_t color=P_MobjIsSelected(u)?0xffffffffu:0xff30d050u;
        V_FillRect((irect_t){rect.x+(int)(at.x*rect.w/level.width),rect.y+(int)(at.y*rect.h/level.height),2,2},V_NearestIndex(color));
    }
    irect_t world=G_WorldViewport(menu->app);
    cell_t tl=R_ScreenToGrid(menu->app,world.x,world.y),br=R_ScreenToGrid(menu->app,world.x+world.w,world.y+world.h);
    V_DrawRectOutline((irect_t){rect.x+tl.x*rect.w/level.width,rect.y+tl.y*rect.h/level.height,
        (br.x-tl.x)*rect.w/level.width,(br.y-tl.y)*rect.h/level.height},V_NearestIndex(0xffffffffu));
}
static bool palette_from(const char *root,const char *name,spritesheet_t *sheet,int colors) {
    char path[2048];native_path(path,sizeof(path),root,name);spritesheet_t ramp={0};
    if(!W_LoadIndexedSheet(path,&ramp))return false;
    memcpy(sheet->palette,ramp.palette,sizeof(sheet->palette));
    for(int i=0;i<colors;i++)sheet->palette[i]=ramp.palette[ramp.lumps[0].indices[i]];
    if(sheet==&wireframe) {
        /* Full-health wireframe maps from Stargus's Starcraft Palettes notes. */
        const int index[]={192,193,208,209,210,211,216,217,218,219};
        const int pixel[]={3,4,1,1,1,1,0,1,18,10};
        for(int i=0;i<10;i++)sheet->palette[index[i]]=ramp.palette[ramp.lumps[0].indices[pixel[i]]];
    }
    sheet->palette[0]=0;memcpy(sheet->source_palette,sheet->palette,sizeof(sheet->palette));
    R_FreeSprite(&ramp);return true;
}

static int append_dialog(const char *root,const char *path) {
    sc_dialog_t d;if(!sc_dialog(root,path,&d)||hud.numitems+d.count>158)return -1;
    int first=hud.numitems;
    for(int i=0;i<d.count;i++)huditems[hud.numitems++]=control(&d.controls[i],gamefonts);
    return first;
}
menu_t *G_InitHUD(app_t *app,const char *root) {
    snprintf(asset_root,sizeof(asset_root),"%s",root);
    char path[2048];native_path(path,sizeof(path),root,"game/tconsole.pcx");
    if(!W_LoadIndexedSheet(path,&console))return NULL;
    if(!grp(root,"unit/wirefram/wirefram.grp",console.palette,&wireframe)||
       !grp(root,"unit/cmdbtns/cmdicons.grp",console.palette,&icons))return NULL;
    if(!palette_from(root,"game/tunit.pcx",&console,0)||
       !palette_from(root,"game/tunit.pcx",&widgets,0)||
       !palette_from(root,"game/tunit.pcx",&panel,0)||
       !palette_from(root,"game/twire.pcx",&wireframe,0)||
       !palette_from(root,"unit/cmdbtns/ticon.pcx",&icons,16))return NULL;
    hud.app=app;hud.refresh=refresh;hud.numitems=1;
    huditems[0]=(menuitem_t){.kind=MI_STATIC,.visible=true,.rect={0,0,640,480},.sheet=&console,.stretch=true};
    int first=append_dialog(root,"rez/minimap.bin");if(first<0)return NULL;
    for(int i=first;i<hud.numitems;i++)huditems[i].visible=false;
    huditems[first].visible=huditems[first].enabled=true;huditems[first].kind=MI_MINIMAP;huditems[first].ownerdraw=minimap;
    first=append_dialog(root,"rez/stat_f10.bin");if(first<0)return NULL;
    huditems[first].routine=open_menu;huditems[first].hotkey=SDLK_F10;huditems[first].sheet=&widgets;
    for(int s=0;s<MS_STATES;s++)huditems[first].look[s].cell=s==MS_PUSHED?2:1;
    portrait_item=append_dialog(root,"rez/statport.bin");if(portrait_item<0)return NULL;
    for(int i=portrait_item;i<hud.numitems;i++)huditems[i].visible=false;
    huditems[portrait_item].kind=MI_STATIC;huditems[portrait_item].ownerdraw=draw_portrait;
    selection_start=append_dialog(root,"rez/statdata.bin");if(selection_start<0)return NULL;
    for(int i=selection_start;i<hud.numitems;i++) {
        if(huditems[i].id==1){wire_item=i;huditems[i].sheet=&wireframe;huditems[i].font=NULL;}
        if(huditems[i].id==65531)name_item=i;
        if(huditems[i].id==65529)hp_item=i;
    }
    command_start=append_dialog(root,"rez/statbtnt.bin");if(command_start<0)return NULL;
    huditems[command_start].sheet=&icons;huditems[command_start].routine=move;huditems[command_start].hotkey=SDLK_m;
    for(int s=0;s<MS_STATES;s++)huditems[command_start].look[s].cell=228;
    huditems[command_start+1].sheet=&icons;huditems[command_start+1].routine=stop;huditems[command_start+1].hotkey=SDLK_s;
    for(int s=0;s<MS_STATES;s++)huditems[command_start+1].look[s].cell=229;
    /* Catalog shortcuts are input-only; retail HUD artwork remains unobscured. */
    for(int i=0;i<2;i++)huditems[hud.numitems++]=(menuitem_t){.kind=MI_BUTTON,.visible=true,.enabled=true,
        .hotkey=i?SDLK_RIGHTBRACKET:SDLK_LEFTBRACKET,.value=i?1:-1,.routine=focus};
    return &hud;
}
void G_ShutdownHUD(void) {
    R_FreeSprite(&console);R_FreeSprite(&wireframe);R_FreeSprite(&icons);R_FreeSprite(&widgets);R_FreeSprite(&panel);R_FreeSprite(&portrait.sheet);free(portrait.palettes);memset(&portrait,0,sizeof(portrait));portrait_id=-1;hudview=(hudview_t){0};
}
