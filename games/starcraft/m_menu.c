#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SC_SELECTION_SLOTS 12

typedef struct { spritesheet_t sheet; unsigned ms; uint32_t *palettes; } movie_t;
typedef struct { sc_control_t native; spritesheet_t image; movie_t movies[SC_CONTROL_MOVIES]; } artwork_t;
static spritesheet_t background, console, wireframe, icons, widgets, panel;
static bitmapfont_t fonts[4], gamefonts[4];
static artwork_t art[SC_DIALOG_CONTROLS];
static menuitem_t items[SC_DIALOG_CONTROLS], pauseitems[SC_DIALOG_CONTROLS], huditems[176];
static menu_t front={.items=items,.size={640,480},.stretch=true,.modal=true};
static menu_t pausemenu={.items=pauseitems,.size={640,480},.stretch=true,.modal=true};
static menu_t hud={.items=huditems,.size={640,480},.stretch=true};
static movie_t portrait;
typedef struct {
    menu_t menu;
    menuitem_t items[SC_DIALOG_CONTROLS];
    artwork_t art[SC_DIALOG_CONTROLS];
    spritesheet_t background;
} screen_t;
static screen_t registry, new_id, campaign, briefing;
static char player_name[32]="Player", campaign_map[256];
static char briefing_text[8192], objectives[2048];
static char asset_root[1024];
static int portrait_item, portrait_id=-1, tip_item;
static int selected_id=-1, selection_start, command_start, wire_item, name_item, hp_item;
static int selection_icon_start, selection_count;
static mobj_t *selection_units[SC_SELECTION_SLOTS];
static char start_tip[512];
static int catalog_index, build_page, resource_start;
static void native_path(char *out,size_t size,const char *root,const char *name) {
    sc_asset_path(out,size,root,name);
}
static menuitem_t control(const sc_control_t *c,bitmapfont_t *f) {
    bool button=c->type==1||c->type==2||c->type==14;
    menuitem_t item={.id=c->id,.kind=button?MI_BUTTON:MI_STATIC,.rect=c->rect,.hitbox=c->hitbox,.flags=c->flags,
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
    if(c->text_offset.x||c->text_offset.y)item.align=0;
    for(int s=0;s<MS_STATES;s++) item.look[s]=(menulook_t){.palette=button?1:0,.cell=-1};
    item.look[MS_FOCUS].palette=2; item.look[MS_PUSHED].palette=2; item.look[MS_DISABLED].palette=3;
    return item;
}
static void begin(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_SetupNextMenu(&registry.menu);
}
static const char *registry_row(const menuitem_t *item,int row) { (void)item;(void)row;return player_name; }
static void frontend(menu_t *m,menuitem_t *i,menuaction_t a) {
    if(a!=MA_ACTIVATE)return;
    if(m==&registry.menu) {
        if(i->id==4)M_SetupNextMenu(&campaign.menu);
        else if(i->id==5)M_SetupNextMenu(&front);
        else if(i->id==6)M_SetupNextMenu(&new_id.menu);
    } else if(m==&new_id.menu) {
        if(i->id==1) {
            const menuitem_t *field=M_MenuFind(m,3);
            if(!field||!field->text[0])return;
            snprintf(player_name,sizeof(player_name),"%.31s",field->text);
        }
        M_SetupNextMenu(&registry.menu);
    } else if(m==&campaign.menu) {
        if(i->id==9){M_SetupNextMenu(&registry.menu);return;}
        const char *race=i->id==7?"terran":i->id==8?"zerg":i->id==6?"protoss":NULL;
        if(!race){M_StartMessage("Saved games and custom scenarios are not available yet.");return;}
        snprintf(campaign_map,sizeof(campaign_map),"install/campaign/%s/%s01/staredit/scenario.chk",race,race);
        char path[2048];snprintf(path,sizeof(path),"%s/%s",asset_root,campaign_map);
        if(!sc_briefing(path,briefing_text,sizeof(briefing_text),objectives,sizeof(objectives))) {
            M_StartMessage("Could not read this campaign briefing.");return;
        }
        M_SetupNextMenu(&briefing.menu);
    } else if(m==&briefing.menu) {
        if(i->id==13){menumap=campaign_map;M_ClearMenus();}
        else if(i->id==14)M_SetupNextMenu(&campaign.menu);
        else if(i->id==20)M_MenuFind(m,65526)->first_row=0;
    }
}
static void resume(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_ClearMenus();
}
static void leave(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE){menuleave=true;M_ClearMenus();}
}
static void unavailable(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_StartMessage("This menu action is not available yet.");
}
static void help(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)M_StartMessage("Drag to select. Right-click to move, attack or gather.\nM: Move   A: Attack   G: Gather   B: Build   S: Stop\nUse the command card to construct and train units.");
}
static void open_menu(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)i;if(a==MA_ACTIVATE)M_StartControlPanel(m->app);
}
static void select_slot(menu_t *menu,menuitem_t *item,menuaction_t action) {
    (void)menu;
    if(action!=MA_ACTIVATE||item->id<0||item->id>=selection_count)return;
    mobj_t *unit=selection_units[item->id];
    if(!unit||unit->remove||unit->hp<=0)return;
    for(int i=0;i<hudview.unit_count;i++)P_MobjSetSelected(hudview.units[i],false);
    P_MobjSetSelected(unit,true);
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

static bool load_screen(screen_t *screen,app_t *app,const char *root,const char *dialog,const char *palette) {
    char path[2048];native_path(path,sizeof(path),root,palette);
    if(!W_LoadIndexedSheet(path,&screen->background))return false;
    sc_dialog_t d;if(!sc_dialog(root,dialog,&d))return false;
    screen->menu=(menu_t){.app=app,.items=screen->items,.numitems=d.count,.size={640,480},
        .stretch=true,.modal=true,.background=&screen->background,.palette=screen->background.palette,.drawitem=draw_front};
    for(int i=0;i<d.count;i++) {
        sc_control_t *c=&d.controls[i];artwork_t *a=&screen->art[i];a->native=*c;
        menuitem_t *item=&screen->items[i];*item=control(c,fonts);item->userdata=a;item->routine=frontend;
        if(c->type==5&&c->text[0]) {
            native_path(path,sizeof(path),root,c->text);
            if(!W_LoadIndexedSheet(path,&a->image))return false;
            item->sheet=&a->image;item->text[0]=0;
            for(int s=0;s<MS_STATES;s++)item->look[s].cell=0;
        }
        for(int j=0;j<c->movie_count;j++) {
            native_path(path,sizeof(path),root,c->movies[j].path);
            movie_t *v=&a->movies[j];if(!sc_movie(path,&v->sheet,&v->ms,&v->palettes))return false;
        }
    }
    return true;
}

bool G_InitMenus(app_t *app,const char *root) {
    snprintf(asset_root,sizeof(asset_root),"%s",root);
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
    if(!load_screen(&registry,app,root,"rez/glulogin.bin","glue/palnl/backgnd.pcx")||
       !load_screen(&new_id,app,root,"rez/glunewch.bin","glue/palnl/backgnd.pcx")||
       !load_screen(&campaign,app,root,"rez/glucmpgn.bin","glue/palcs/backgnd.pcx")||
       !load_screen(&briefing,app,root,"rez/glurdyt.bin","glue/palrt/backgnd.pcx"))return false;
    menuitem_t *list=M_MenuFind(&registry.menu,8);
    list->kind=MI_LIST;list->rows=1;list->row_height=20;list->row=registry_row;list->value=0;list->enabled=true;
    M_MenuFind(&registry.menu,7)->enabled=false;
    menuitem_t *field=M_MenuFind(&new_id.menu,3);
    field->kind=MI_TEXTFIELD;field->maxchars=31;field->enabled=field->visible=true;
    snprintf(field->text,sizeof(field->text),"%s",player_name);
    new_id.menu.itemOn=(int)(field-new_id.items);
    for(int i=0;i<new_id.menu.numitems;i++) {
        new_id.items[i].rect.x+=(640-220)/2;new_id.items[i].rect.y+=(480-100)/2;
    }
    menuitem_t *text=M_MenuFind(&briefing.menu,65526);
    text->kind=MI_STATIC;text->visible=true;text->prose=briefing_text;
    text=M_MenuFind(&briefing.menu,65525);text->visible=true;text->prose=objectives;
    M_MenuFind(&briefing.menu,65524)->visible=false;
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
    screen_t *screens[]={&registry,&new_id,&campaign,&briefing};
    for(unsigned s=0;s<sizeof(screens)/sizeof(*screens);s++) {
        screen_t *screen=screens[s];R_FreeSprite(&screen->background);
        for(int i=0;i<SC_DIALOG_CONTROLS;i++) {
            R_FreeSprite(&screen->art[i].image);
            for(int j=0;j<SC_CONTROL_MOVIES;j++) {
                R_FreeSprite(&screen->art[i].movies[j].sheet);free(screen->art[i].movies[j].palettes);
            }
        }
        memset(screen,0,sizeof(*screen));
    }
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
static void command(menu_t *m,menuitem_t *i,menuaction_t a) {
    if(a==MA_ACTIVATE)M_MenuTarget(m,i);
    if(a!=MA_TARGET)return;
    ivec2_t at=R_ScreenToMapGrid(m->app,&level,m->cursor.x,m->cursor.y);
    uint32_t target=0;
    if(i->value==TC_ATTACK) {
        int picked=R_PickUnit(m->app,&level,hudview.units,hudview.unit_count,NULL,
            hudview.sprites,gameinfo,m->cursor.x,m->cursor.y,-1);
        if(picked<0)return;
        target=hudview.units[picked]->id;
    }
    HU_SelectedOrder(i->value,(fvec2_t){at.x,at.y},target);
}
static void build_menu(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;if(a==MA_ACTIVATE)build_page=i->value;
}
static void product(menu_t *m,menuitem_t *i,menuaction_t a) {
    if(a==MA_TARGET){HU_PlaceProduct(m,i,a);return;}
    if(a!=MA_ACTIVATE)return;
    for(int j=0;j<hudview.unit_count;j++) {
        mobj_t *u=hudview.units[j];
        if(P_MobjIsSelected(u)&&u->owner==consoleplayer) {
            const StaticProductDefinition *p=G_ModelProductByUIId(NULL,i->value);
            if(p)HU_BuildProduct(m,i,u,p);
            return;
        }
    }
}
static void button(int slot,int icon,SDL_Keycode key,menuroutine_t routine,int value,const char *tip) {
    menuitem_t *i=&huditems[command_start+slot];
    i->visible=i->enabled=true;i->kind=MI_BUTTON;i->sheet=&icons;i->font=NULL;
    i->hotkey=key;i->routine=routine;i->value=value;i->text[0]=0;i->tooltip=tip;
    i->drawtarget=routine==product?HU_DrawProductPlacement:NULL;
    for(int s=0;s<MS_STATES;s++)i->look[s].cell=icon;
}
static void align_grp_cells(spritesheet_t *sheet) {
    for(int i=0;i<sheet->numlumps;i++) {
        spritecell_t *cell=&sheet->cells[i];
        cell->displacement=ivec2_sub(
            (ivec2_t){sheet->frame_size.w/2,sheet->frame_size.h/2},cell->ground_point);
    }
}
static void refresh(menu_t *menu) {
    (void)menu;mobj_t *selected=NULL;
    selection_count=0;
    memset(selection_units,0,sizeof(selection_units));
    for(int i=0;i<hudview.unit_count;i++)if(P_MobjIsSelected(hudview.units[i])&&hudview.units[i]->hp>0) {
        if(!selected)selected=hudview.units[i];
        if(selection_count<SC_SELECTION_SLOTS)
            selection_units[selection_count++]=hudview.units[i];
    }
    int id_now=selected?selected->type_id-1:-1;
    if(id_now!=selected_id){build_page=0;hud.target=NULL;}
    selected_id=id_now;
    snprintf(huditems[resource_start].text,128,"Minerals %d",level.player_resources[consoleplayer][0]);
    snprintf(huditems[resource_start+1].text,128,"Gas %d",level.player_resources[consoleplayer][1]);
    for(int i=selection_start;i<command_start;i++)huditems[i].visible=false;
    for(int i=0;i<9;i++)huditems[command_start+i].visible=false;
    huditems[portrait_item].visible=false;
    for(int i=0;i<SC_SELECTION_SLOTS;i++)huditems[selection_icon_start+i].visible=false;
    huditems[tip_item].visible=start_tip[0];
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
    huditems[wire_item].visible=selection_count==1&&selected_id<wireframe.numlumps;
    huditems[name_item].visible=selection_count==1;
    huditems[hp_item].visible=selection_count==1;
    for(int s=0;s<MS_STATES;s++)huditems[wire_item].look[s].cell=selected_id;
    if(selection_count>1)for(int i=0;i<selection_count;i++) {
        menuitem_t *icon=&huditems[selection_icon_start+i];
        icon->visible=icon->enabled=true;
        int frame=(int)selection_units[i]->type_id-1;
        for(int s=0;s<MS_STATES;s++)icon->look[s].cell=frame;
    }
    if(selected->production) snprintf(huditems[hp_item].text,128,"%s %d%%",
        selected->production->placed?"Building":"Training",
        100-selected->production->time_left_ms*100/selected->production->time_ms);
    if(selected->owner!=consoleplayer)return;
    if(!build_page) {
        if(selected->traits&MF_MOBILE) {
            button(0,228,SDLK_m,command,TC_MOVE,"Move");
            button(1,229,SDLK_s,stop,0,"Stop");
        }
        if(selected->traits&MF_ATTACK)button(2,230,SDLK_a,command,TC_ATTACK,"Attack");
        if(selected->traits&MF_HARVESTER) {
            button(4,231,SDLK_g,command,TC_HARVEST,"Gather");
            button(6,234,SDLK_b,build_menu,1,"Build Structure");
            button(7,235,SDLK_v,build_menu,2,"Advanced Structures");
        }
    }
    StaticProductDefinition list[SC_TYPES];int n=G_ModelGetProducts(NULL,consoleplayer,list,SC_TYPES),slot=0;
    static const SDL_Keycode basic[]={SDLK_c,SDLK_s,SDLK_r,SDLK_b,SDLK_a,SDLK_e,SDLK_t,SDLK_u};
    for(int i=0;i<n;i++) {
        const StaticProductDefinition *p=&list[i];
        if(p->makers[0]!=selected->type_id)continue;
        if(p->product_class==RTS_PRODUCT_BUILDING) {
            if(!build_page)continue;
            int index=slot++;
            if((build_page==1&&index>=8)||(build_page==2&&index<8))continue;
            int pos=build_page==1?index:index-8;
            button(pos,p->icon_frame,selected->type_id==8&&build_page==1?basic[pos]:SDLK_1+pos,product,p->ui_id,p->label);
            huditems[command_start+pos].enabled=G_ModelProductAvailable(NULL,consoleplayer,p);
        } else if(!build_page) {
            button(slot,p->icon_frame,slot==0?SDLK_t:SDLK_1+slot,product,p->ui_id,p->label);
            huditems[command_start+slot++].enabled=G_ModelProductAvailable(NULL,consoleplayer,p);
        }
    }
    if(build_page)button(8,236,SDLK_ESCAPE,build_menu,0,"Cancel");
}
static void minimap(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    const tileset_t *tiles=hudview.tileset;
    if (!tiles || !tiles->indices || !level.tile_ids || level.width<1 || level.height<1 || rect.w<1 || rect.h<1) return;
    irect_t clip=V_GetClip();V_SetClip(rect);
    for (int y=0;y<rect.h;y++) for (int x=0;x<rect.w;x++) {
        int tile=level.tile_ids[(y*level.height/rect.h)*level.width+x*level.width/rect.w];
        tile=tiles->tile_lookup[tile];
        V_DrawPoint(ivec2_add((ivec2_t){rect.x,rect.y},(ivec2_t){x,y}),tiles->indices[tile*1024+16*32+16]);
    }
    R_DrawMinimapFog(&level, rect);
    for (int i=0;i<hudview.unit_count;i++) {
        const mobj_t *u=hudview.units[i];
        if (u->hp <= 0 || !P_VisibleToPlayer(u)) continue;
        fvec2_t at=fixed3_xy_to_fvec2(u->core.position);
        uint32_t color=P_MobjIsSelected(u)?0xffffffffu:0xff30d050u;
        V_FillRect((irect_t){rect.x+(int)(at.x*rect.w/level.width),rect.y+(int)(at.y*rect.h/level.height),2,2},V_NearestIndex(color));
    }
    irect_t world=G_WorldViewport(menu->app);
    cell_t tl=R_ScreenToGrid(menu->app,world.x,world.y),br=R_ScreenToGrid(menu->app,world.x+world.w,world.y+world.h);
    V_DrawRectOutline((irect_t){rect.x+tl.x*rect.w/level.width,rect.y+tl.y*rect.h/level.height,
        (br.x-tl.x)*rect.w/level.width,(br.y-tl.y)*rect.h/level.height},V_NearestIndex(0xffffffffu));
    V_SetClip(clip);
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
    align_grp_cells(&icons);
    if(!palette_from(root,"game/tunit.pcx",&console,0)||
       !palette_from(root,"game/tunit.pcx",&widgets,0)||
       !palette_from(root,"game/tunit.pcx",&panel,0)||
       !palette_from(root,"game/twire.pcx",&wireframe,0)||
       !palette_from(root,"unit/cmdbtns/ticon.pcx",&icons,16))return NULL;
    hud.app=app;hud.refresh=refresh;hud.numitems=1;
    huditems[0]=(menuitem_t){.kind=MI_STATIC,.visible=true,.rect={0,0,640,480},.passthrough=true,.sheet=&console,.stretch=true};
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
    resource_start=hud.numitems;
    for(int r=0;r<2;r++)huditems[hud.numitems++]=(menuitem_t){.kind=MI_STATIC,.visible=true,
        .rect={r?500:350,0,140,20},.font=&gamefonts[0],.align=MALIGN_RIGHT};
    /* Catalog shortcuts are input-only; retail HUD artwork remains unobscured. */
    for(int i=0;i<2;i++)huditems[hud.numitems++]=(menuitem_t){.kind=MI_BUTTON,.visible=true,.enabled=true,
        .hotkey=i?SDLK_RIGHTBRACKET:SDLK_LEFTBRACKET,.value=i?1:-1,.routine=focus};
    selection_icon_start=hud.numitems;
    for(int i=0;i<SC_SELECTION_SLOTS;i++) {
        ivec2_t cell={(i%6)*36,(i/6)*37};
        huditems[hud.numitems++]=(menuitem_t){.id=i,.kind=MI_BUTTON,.visible=false,.enabled=false,
            .rect={168+cell.x,396+cell.y,33,34},.sheet=&wireframe,.stretch=true,
            .routine=select_slot,.opaque=false};
    }
    sc_start_tip(&level,start_tip,sizeof(start_tip));
    tip_item=hud.numitems++;
    huditems[tip_item]=(menuitem_t){.kind=MI_STATIC,.visible=false,.passthrough=true,.rect={58,190,300,112},
        .font=&gamefonts[0],.prose=start_tip,.opaque=false};
    return &hud;
}
void G_ShutdownHUD(void) {
    R_FreeSprite(&console);R_FreeSprite(&wireframe);R_FreeSprite(&icons);R_FreeSprite(&widgets);R_FreeSprite(&panel);R_FreeSprite(&portrait.sheet);free(portrait.palettes);memset(&portrait,0,sizeof(portrait));portrait_id=-1;hudview=(hudview_t){0};
}
