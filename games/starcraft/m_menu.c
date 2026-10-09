#include "sc_local.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SC_SELECTION_SLOTS 12

typedef struct { spritesheet_t sheet; unsigned ms; uint32_t *palettes; } movie_t;
typedef struct { sc_control_t native; spritesheet_t image; movie_t movies[SC_CONTROL_MOVIES]; } artwork_t;
typedef struct theme_s {
    char path[128];
    spritesheet_t background, widgets, panel;
    bitmapfont_t fonts[4];
    struct theme_s *next;
} theme_t;
static theme_t *themes;
/* In-level dialogs draw on the player's race art (dlgs/<race>.grp, the same
 * frame layout as glue dlg.grp) and the game fonts; the world stays behind. */
static theme_t ingame;
static spritesheet_t console, wireframe, icons;
static bitmapfont_t gamefonts[4];
static menuitem_t huditems[176];
static menu_t front;
static menu_t hud={.items=huditems,.size={640,480},.stretch=true};
static char restart_path[1024], next_path[256];
static int shown_result;
static movie_t portrait;
typedef struct {
    menu_t menu;
    menuitem_t items[SC_DIALOG_CONTROLS];
    artwork_t art[SC_DIALOG_CONTROLS];
    theme_t *theme;
} screen_t;
static screen_t main_screen, registry, new_id, campaign, conn, join, create, chat;
static screen_t briefings[3], load_game, custom, notice, confirm;
static screen_t *briefing=&briefings[0];
static char player_name[32]="Player", campaign_map[256], custom_map[512];
static char briefing_text[8192], objectives[2048];
static char asset_root[1024];
static int portrait_item, portrait_id=-1, tip_item;
static int selected_id=-1, selection_start, command_start, wire_item, name_item, hp_item;
static int selection_icon_start, selection_count;
static mobj_t *selection_units[SC_SELECTION_SLOTS];
static char start_tip[512];
static int catalog_index, build_page, resource_start;
static void multi(menu_t *menu,menuitem_t *item,menuaction_t action);
static void native_path(char *out,size_t size,const char *root,const char *name) {
    sc_asset_path(out,size,root,name);
}
static menuitem_t control(const sc_control_t *c,bitmapfont_t *f) {
    bool button=c->type==1||c->type==2||c->type==14;
    menuitem_t item={.id=c->id,.kind=button?MI_BUTTON:MI_STATIC,.rect=c->rect,.hitbox=c->hitbox,.flags=c->flags,
        .visible=(c->flags&8)!=0,.enabled=!(c->flags&2),.release=true,.inset=c->text_offset,
        .hotkey=c->hotkey<128?tolower(c->hotkey):0,.mark_at=c->mark_at,.mark_len=c->mark_len};
    if(c->type==8||c->type==13)
        item.inset=ivec2_add(item.inset,(ivec2_t){4,0});
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
static void show_notice(const char *text);
static void show_score(void);
static void show_load(void);
static void show_custom(void);
static bool is_briefing(const menu_t *m) {
    for(int r=0;r<3;r++)if(m==&briefings[r].menu)return true;
    return false;
}
/* The MBRF script plays as in the original: portraits come and go in the
 * four frames (ids 15..18), and each transmission replaces the text and makes
 * its speaker's portrait talk for the length of its line. The voice is not
 * played. Times are milliseconds since the briefing (or Replay) began. */
static sc_briefing_t script;
static struct { movie_t idle, talk; unsigned talk_until; bool shown; } slots[4];
static int script_at;
static unsigned script_clock, script_due, script_tick, text_from, text_for;
static bool script_text;
static void free_movie(movie_t *movie) {
    R_FreeSprite(&movie->sheet);free(movie->palettes);memset(movie,0,sizeof(*movie));
}
static void hide_slot(int slot) {
    free_movie(&slots[slot].idle);free_movie(&slots[slot].talk);
    slots[slot].shown=false;slots[slot].talk_until=0;
}
static void show_slot(int slot,unsigned unit) {
    hide_slot(slot);
    if(unit>=SC_TYPES)return;
    char path[2048];
    int id=sc_units[unit].portrait;
    if(sc_portrait_movie(asset_root,id,false,path,sizeof(path)))
        sc_movie(path,&slots[slot].idle.sheet,&slots[slot].idle.ms,&slots[slot].idle.palettes);
    if(sc_portrait_movie(asset_root,id,true,path,sizeof(path)))
        sc_movie(path,&slots[slot].talk.sheet,&slots[slot].talk.ms,&slots[slot].talk.palettes);
    slots[slot].shown=slots[slot].idle.sheet.numlumps>0;
}
static void say(int text,unsigned length) {
    if(text<0)return;
    snprintf(briefing_text,sizeof(briefing_text),"%s",script.strings+text);
    text_from=script_due;text_for=length;
}
static void restart_script(void) {
    for(int i=0;i<4;i++)hide_slot(i);
    script_at=0;script_clock=script_due=0;text_from=text_for=0;
    script_tick=SDL_GetTicks();
    if(script_text)briefing_text[0]=0;
}
static void run_script(unsigned ms) {
    script_clock+=ms;
    while(script_at<script.count&&script_clock>=script_due) {
        const sc_brief_action_t *a=&script.actions[script_at++];
        int slot=a->slot<4?a->slot:-1;
        switch(a->op) {
        case SC_BRIEF_WAIT: script_due+=(unsigned)a->time; break;
        case SC_BRIEF_TEXT: say(a->text,(unsigned)a->time); break;
        case SC_BRIEF_SHOW_PORTRAIT: if(slot>=0)show_slot(slot,a->unit); break;
        case SC_BRIEF_HIDE_PORTRAIT: if(slot>=0)hide_slot(slot); break;
        case SC_BRIEF_TALK: if(slot>=0)slots[slot].talk_until=script_due+(unsigned)a->time; break;
        case SC_BRIEF_TRANSMISSION:
            say(a->text,(unsigned)a->time);
            if(slot>=0)slots[slot].talk_until=script_due+(unsigned)a->time;
            script_due+=(unsigned)a->time;
            break;
        }
    }
}
/* Advances the briefing clock without waiting (tests, and nothing else). */
void sc_briefing_advance(unsigned ms) { run_script(ms); }
bool sc_briefing_slot(int slot,bool *talking) {
    if(slot<0||slot>=4)return false;
    if(talking)*talking=script_clock<slots[slot].talk_until&&slots[slot].talk.sheet.numlumps>0;
    return slots[slot].shown;
}
/* A 60x56 portrait drawn twice its size in the middle of the 128x118 frame. */
static void draw_slot(const menu_t *menu,const menuitem_t *item,irect_t rect) {
    (void)menu;
    int slot=item->id-15;
    if(slot<0||slot>=4||!slots[slot].shown)return;
    bool talking=false;sc_briefing_slot(slot,&talking);
    const movie_t *movie=talking?&slots[slot].talk:&slots[slot].idle;
    if(!movie->sheet.numlumps||!movie->ms)return;
    unsigned frame=SDL_GetTicks()/movie->ms%movie->sheet.numlumps;
    isize2_t size=movie->sheet.frame_size;
    int scale=item->rect.w>0?rect.w/item->rect.w:1;if(scale<1)scale=1;
    irect_t dst={0,0,size.w*2*scale,size.h*2*scale};
    dst.x=rect.x+(rect.w-dst.w)/2;dst.y=rect.y+(rect.h-dst.h)/2;
    R_DrawIndexed(movie->sheet.lumps[frame].indices,size,movie->palettes+frame*256,NULL,NULL,&dst,V_OPAQUE);
}
static int briefing_lines(const menuitem_t *text) {
    if(!text->font||text->font->line_h<1)return 0;
    return (V_TextWrappedHeight(text->rect.w,text->font,text->prose?text->prose:"")+text->font->line_h-1)/text->font->line_h;
}
/* glurdyt, glurdyz and glurdyp share their ids; the campaign folder picks one. */
static bool open_briefing(const char *rel) {
    char map[sizeof(campaign_map)],path[2048];
    snprintf(map,sizeof(map),"%s",rel);
    screen_t *screen=&briefings[strstr(map,"/zerg/")?1:strstr(map,"/protoss/")?2:0];
    if(!screen->menu.numitems)return false;
    snprintf(path,sizeof(path),"%s/%s",asset_root[0]?asset_root:g_game_default_root,map);
    if(!sc_briefing(path,briefing_text,sizeof(briefing_text),objectives,sizeof(objectives)))return false;
    if(!sc_briefing_script(path,&script))script.count=0;
    script_text=false;
    for(int i=0;i<script.count;i++)
        if(script.actions[i].op==SC_BRIEF_TRANSMISSION||script.actions[i].op==SC_BRIEF_TEXT)script_text=true;
    snprintf(campaign_map,sizeof(campaign_map),"%s",map);
    briefing=screen;
    M_MenuFind(&screen->menu,65526)->first_row=0;
    restart_script();
    run_script(0);
    M_SetupNextMenu(&screen->menu);
    return true;
}
/* A transmission longer than the box rises through it over its own length,
 * after holding its first lines for a quarter of the time. */
static void briefing_tick(menu_t *menu) {
    M_MenuTicker(menu);
    unsigned now=SDL_GetTicks();
    run_script(now-script_tick);
    script_tick=now;
    menuitem_t *text=M_MenuFind(menu,65526);
    int overflow=briefing_lines(text)-(text->font?text->rect.h/text->font->line_h:0);
    unsigned into=script_clock>text_from?script_clock-text_from:0,hold=text_for/4;
    int line=overflow>0&&text_for>hold&&into>hold?(int)((into-hold)*(unsigned)overflow/(text_for-hold)):0;
    text->first_row=overflow<=0?0:line<overflow?line:overflow;
}
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
        if(i->id==5){show_load();return;}
        if(i->id==10){show_custom();return;}
        const char *race=i->id==7?"terran":i->id==8?"zerg":i->id==6?"protoss":NULL;
        if(!race)return;
        char map[sizeof(campaign_map)];
        snprintf(map,sizeof(map),"install/campaign/%s/%s01/staredit/scenario.chk",race,race);
        if(!open_briefing(map))show_notice("Could not read this campaign briefing.");
    } else if(is_briefing(m)) {
        if(i->id==13){sc_set_net_races(NULL);sc_set_custom_slots(NULL,NULL);menumap=campaign_map;M_ClearMenus();}
        else if(i->id==14)M_SetupNextMenu(&campaign.menu);
        else if(i->id==20){restart_script();run_script(0);}
    }
}
/* Escape is each screen's Cancel. */
static void single_escape(menu_t *m) {
    if(m==&registry.menu)M_SetupNextMenu(&front);
    else if(m==&new_id.menu||m==&campaign.menu)M_SetupNextMenu(&registry.menu);
    else M_SetupNextMenu(&campaign.menu);
}
static void unavailable(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)show_notice("This menu action is not available yet.");
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
/* Native three-piece chrome; input and the scrollbar thumb stay in m_menu. */
static void draw_glue(const menu_t *menu,const spritesheet_t *sheet,int frame,irect_t rect) {
    if(rect.w<=0||rect.h<=0)return;
    menuitem_t picture={.rect=rect};
    irect_t dst=M_MenuItemRect(menu,&picture);
    R_DrawSprite(sheet,frame,-1,&sheet->cells[frame].rect,&dst,0,16);
}
static void draw_glue_strip(const menu_t *menu,const spritesheet_t *sheet,irect_t rect,int first,bool vertical) {
    irect_t a=sheet->cells[first].rect,c=sheet->cells[first+2].rect;
    if(vertical) {
        draw_glue(menu,sheet,first,(irect_t){rect.x,rect.y,rect.w,a.h});
        draw_glue(menu,sheet,first+1,(irect_t){rect.x,rect.y+a.h,rect.w,rect.h-a.h-c.h});
        draw_glue(menu,sheet,first+2,(irect_t){rect.x,rect.y+rect.h-c.h,rect.w,c.h});
    } else {
        rect.y+=(rect.h-a.h)/2;rect.h=a.h;
        draw_glue(menu,sheet,first,(irect_t){rect.x,rect.y,a.w,rect.h});
        draw_glue(menu,sheet,first+1,(irect_t){rect.x+a.w,rect.y,rect.w-a.w-c.w,rect.h});
        draw_glue(menu,sheet,first+2,(irect_t){rect.x+rect.w-c.w,rect.y,c.w,rect.h});
    }
}
/* One piece, cropped to w by h of its picture; it never stretches. */
static void draw_glue_part(const menu_t *menu,const spritesheet_t *sheet,int frame,irect_t rect) {
    if(rect.w<=0||rect.h<=0)return;
    menuitem_t picture={.rect=rect};
    irect_t dst=M_MenuItemRect(menu,&picture),src=sheet->cells[frame].rect;
    src.w=rect.w<src.w?rect.w:src.w;src.h=rect.h<src.h?rect.h:src.h;
    R_DrawSprite(sheet,frame,-1,&src,&dst,0,16);
}
/* Nine pieces: corners once, edges and centre repeated (tile.grp's 8x8). */
static void draw_glue_panel(const menu_t *menu,const spritesheet_t *sheet,irect_t rect,int base) {
    if(sheet->numlumps<base+9)return;
    irect_t first=sheet->cells[base].rect,last=sheet->cells[base+8].rect;
    int x[]={rect.x,rect.x+first.w,rect.x+rect.w-last.w};
    int y[]={rect.y,rect.y+first.h,rect.y+rect.h-last.h};
    int w[]={first.w,rect.w-first.w-last.w,last.w};
    int h[]={first.h,rect.h-first.h-last.h,last.h};
    for(int row=0;row<3;row++)for(int col=0;col<3;col++) {
        int frame=base+row*3+col;
        irect_t piece=sheet->cells[frame].rect;
        if(piece.w<1||piece.h<1)continue;
        for(int py=0;py<h[row];py+=piece.h)for(int px=0;px<w[col];px+=piece.w)
            draw_glue_part(menu,sheet,frame,(irect_t){x[col]+px,y[row]+py,
                w[col]-px<piece.w?w[col]-px:piece.w,h[row]-py<piece.h?h[row]-py:piece.h});
    }
}
/* The open list is drawn on a copy of the combobox. Its screen rect maps
 * back to dialog units through the combobox's own scale. */
static bool drops_up(const menu_t *menu,irect_t popup) {
    return popup.y<M_MenuItemRect(menu,menu->dropdown).y;
}
static void draw_drop_panel(const menu_t *menu,const spritesheet_t *sheet,irect_t popup) {
    const menuitem_t *box=menu->dropdown;
    irect_t screen=M_MenuItemRect(menu,box);
    if(screen.h<=0)return;
    /* The panel meets the box's drawn edge, which is centred in its rect,
     * and is never shorter than its corners; one row is not. */
    int gap=(box->rect.h-sheet->cells[47].rect.h)/2;
    int h=popup.h*box->rect.h/screen.h+gap,least=sheet->cells[35].rect.h+sheet->cells[41].rect.h;
    if(h<least)h=least;
    irect_t rect={box->rect.x,drops_up(menu,popup)?box->rect.y+gap-h:box->rect.y+box->rect.h-gap,box->rect.w,h};
    draw_glue_panel(menu,sheet,rect,35);
}
static menu_t *beneath;
static bool draw_front(const menu_t *menu,const menuitem_t *item,menustate_t state,irect_t rect) {
    const theme_t *theme=menu->owner;
    /* A popup's own root draws the screen it covers first. */
    if(item->id==-4&&beneath&&beneath!=menu&&(menu==&notice.menu||menu==&confirm.menu))
        M_MenuDrawer(beneath);
    const spritesheet_t *sheet=&theme->widgets;
    if(item->kind==MI_SCROLLBAR && item->sheet==sheet)
        draw_glue_strip(menu,sheet,item->rect,29,true);
    /* dlg.grp 35..43 is the drop panel; the copy carries the combobox id. */
    if(item->kind==MI_LIST && menu->dropdown && item->id==menu->dropdown->id)
        draw_drop_panel(menu,sheet,rect);
    if(item->kind==MI_DROPDOWN) {
        int arrow=state==MS_DISABLED?52:state==MS_NORMAL?50:51;
        irect_t part=sheet->cells[arrow].rect;
        int box=state==MS_FOCUS||state==MS_PUSHED?56:53;
        /* 44..46 open upward, 47..49 downward: the edge toward the list is
         * square. The shared menu opens upward only when below lacks room. */
        if(menu->dropdown==item) {
            int below=menu->size.h-item->rect.y-item->rect.h;
            box=below<item->rows*item->row_height&&item->rect.y>below?44:47;
        }
        draw_glue_strip(menu,sheet,item->rect,box,false);
        /* PyMS WidgetNode's native combobox preview places the arrow 5px
         * inside the right edge and centres it vertically. */
        draw_glue(menu,sheet,arrow,(irect_t){item->rect.x+item->rect.w-part.w-5,
                  item->rect.y+(item->rect.h-part.h)/2,part.w,part.h});
    }
    const artwork_t *a=item->userdata;
    if(!a)return false;
    const sc_control_t *c=&a->native;
    if(!c->type&&!item->sheet)draw_glue_panel(menu,&theme->panel,item->rect,0);
    if(c->type==1||c->type==2)
        draw_glue_strip(menu,sheet,item->rect,state==MS_PUSHED?118:state==MS_DISABLED?112:115,false);
    if(c->type==3||c->type==4) {
        bool pressed=(menu->held==item&&menu->over)||menu->keyheld==item;
        int first=c->type==3?6:11;
        int frame=state==MS_DISABLED?first:first+(item->value?3:1)+pressed;
        irect_t part=sheet->cells[frame].rect;
        draw_glue(menu,sheet,frame,(irect_t){item->rect.x,item->rect.y+(item->rect.h-part.h)/2,part.w,part.h});
    }
    if(c->type==6)draw_glue_strip(menu,sheet,item->rect,state==MS_DISABLED?91:94,false);
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
static theme_t *load_theme(const char *root,const char *palette) {
    char directory[128];snprintf(directory,sizeof(directory),"%s",palette);
    char *slash=strrchr(directory,'/');if(!slash)return NULL;*slash=0;
    for(theme_t *theme=themes;theme;theme=theme->next)
        if(!strcmp(theme->path,directory))return theme;
    theme_t *theme=calloc(1,sizeof(*theme));if(!theme)return NULL;
    theme->next=themes;themes=theme;
    snprintf(theme->path,sizeof(theme->path),"%s",directory);
    char path[2048],name[256];native_path(path,sizeof(path),root,palette);
    if(!W_LoadIndexedSheet(path,&theme->background))return NULL;
    snprintf(name,sizeof(name),"%s/dlg.grp",directory);
    if(!grp(root,name,theme->background.palette,&theme->widgets)||theme->widgets.numlumps<139)return NULL;
    snprintf(name,sizeof(name),"%s/tile.grp",directory);
    if(!grp(root,name,theme->background.palette,&theme->panel)||theme->panel.numlumps!=9)return NULL;
    const char *names[]={"font10","font14","font16","font16x"};
    snprintf(name,sizeof(name),"%s/tfont.pcx",directory);
    for(int i=0;i<4;i++)
        if(!sc_font(root,names[i],&theme->fonts[i])||!sc_font_colors(root,name,&theme->fonts[i]))return NULL;
    return theme;
}
/* The scroll bar shows only while rows overflow the list. gluconn's list
 * runs 6px past pListSml's frame, so an idle bar there would too. */
static void fit_scrollbars(menu_t *menu) {
    for(int i=0;i<menu->numitems;i++) {
        menuitem_t *item=&menu->items[i];
        if(item->kind!=MI_SCROLLBAR&&!item->step)continue;
        const menuitem_t *list=&menu->items[item->link];
        int page=list->row_height>0?list->rect.h/list->row_height:0;
        item->visible=item->enabled=list->visible&&list->rows>page;
    }
}
static bool add_scrollbar(screen_t *screen,int index) {
    if(screen->menu.numitems+3>SC_DIALOG_CONTROLS)return false;
    menuitem_t *list=&screen->items[index];
    const spritesheet_t *sheet=&screen->theme->widgets;
    irect_t bounds=list->rect,arrow=sheet->cells[17].rect;
    list->rect.w-=arrow.w;
    /* PyMS places the track two pixels clear of each arrow inside the
     * native list rectangle. The shared menu owns scrolling and dragging. */
    menuitem_t *bar=&screen->items[screen->menu.numitems++];
    *bar=(menuitem_t){.kind=MI_SCROLLBAR,.id=-1,.visible=list->visible,.enabled=list->enabled,
        .rect={bounds.x+bounds.w-arrow.w,bounds.y+arrow.h+2,arrow.w,bounds.h-2*(arrow.h+2)},
        .sheet=sheet,.link=index,.thumb={.cell=28,.palette=-1,.part=sheet->cells[28].rect}};
    for(int s=0;s<MS_STATES;s++)bar->look[s].cell=-1;
    for(int i=0;i<2;i++) {
        menuitem_t *step=&screen->items[screen->menu.numitems++];
        *step=(menuitem_t){.kind=MI_BUTTON,.id=-2-i,.visible=list->visible,.enabled=list->enabled,.release=true,
            .rect={bar->rect.x,bounds.y+i*(bounds.h-arrow.h),arrow.w,arrow.h},
            .sheet=sheet,.stretch=true,.disabled_look=true,.link=index,.step=i?1:-1};
        for(int s=0;s<MS_STATES;s++) {
            int frame=(i?20:17)+(s==MS_PUSHED?1:s==MS_DISABLED?-1:0);
            step->look[s]=(menulook_t){.cell=frame,.palette=-1,.part=sheet->cells[frame].rect};
        }
    }
    return true;
}
/* A popup's root is the theme's popup picture (pOPopup, pDPopup) when one is named. */
static bool load_popup(screen_t *screen,app_t *app,const char *root,const char *dialog,const char *palette,
                       const char *picture,menuroutine_t routine);
static bool load_screen(screen_t *screen,app_t *app,const char *root,const char *dialog,const char *palette,menuroutine_t routine) {
    return load_popup(screen,app,root,dialog,palette,NULL,routine);
}
static bool build_screen(screen_t *screen,app_t *app,const char *root,const char *dialog,theme_t *theme,
                         const char *picture,menuroutine_t routine);
static bool load_popup(screen_t *screen,app_t *app,const char *root,const char *dialog,const char *palette,
                       const char *picture,menuroutine_t routine) {
    theme_t *theme=load_theme(root,palette);
    return theme&&build_screen(screen,app,root,dialog,theme,picture,routine);
}
static bool build_screen(screen_t *screen,app_t *app,const char *root,const char *dialog,theme_t *theme,
                         const char *picture,menuroutine_t routine) {
    char path[2048];screen->theme=theme;
    sc_dialog_t d;if(!sc_dialog(root,dialog,&d))return false;
    screen->menu=(menu_t){.app=app,.items=screen->items,.numitems=d.count,.size={640,480},
        .stretch=true,.modal=true,.background=&screen->theme->background,.palette=screen->theme->background.palette,
        .drawitem=draw_front,.owner=screen->theme,.refresh=fit_scrollbars};
    bool popup=d.rect.w<640&&d.rect.h<480;
    ivec2_t offset=popup?(ivec2_t){(640-d.rect.w)/2-d.rect.x,(480-d.rect.h)/2-d.rect.y}:(ivec2_t){0,0};
    if(popup) {
        if(d.count==SC_DIALOG_CONTROLS)return false;
        artwork_t *a=&screen->art[d.count];a->native.rect=d.rect;
        a->native.rect.x+=offset.x;a->native.rect.y+=offset.y;
        screen->items[0]=(menuitem_t){.kind=MI_STATIC,.id=-4,.visible=true,.rect=a->native.rect,.userdata=a};
        screen->menu.numitems++;
        if(picture) {
            snprintf(path,sizeof(path),"%s/%s",screen->theme->path,picture);
            char full[2048];native_path(full,sizeof(full),root,path);
            if(!W_LoadIndexedSheet(full,&a->image))return false;
            screen->items[0].sheet=&a->image;
            for(int s=0;s<MS_STATES;s++)screen->items[0].look[s]=(menulook_t){.cell=0};
        }
    }
    for(int i=0;i<d.count;i++) {
        sc_control_t *c=&d.controls[i];artwork_t *a=&screen->art[i];a->native=*c;
        menuitem_t *item=&screen->items[i+popup];*item=control(c,screen->theme->fonts);item->userdata=a;item->routine=routine;
        item->rect.x+=offset.x;item->rect.y+=offset.y;
        item->disabled_look=true;
        if(c->type==3||c->type==4) {
            item->kind=MI_CHECK;item->group=c->type==3?1:0;
            item->inset.x+=screen->theme->widgets.cells[c->type==3?9:14].rect.w+4;
        } else if(c->type==6) {
            item->kind=MI_SLIDER;item->sheet=&screen->theme->widgets;
            item->thumb=(menulook_t){.cell=100,.palette=-1,.part=item->sheet->cells[100].rect};
        } else if(c->type==8)item->kind=MI_TEXTFIELD;
        else if(c->type==12)item->kind=MI_LIST;
        else if(c->type==13) {
            item->kind=MI_DROPDOWN;item->align=MALIGN_VCENTER;item->look[MS_NORMAL].palette=1;
        }
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
    for(int i=0;i<d.count;i++)
        if(d.controls[i].type==12&&!add_scrollbar(screen,i+popup))return false;
    return true;
}

/* Extracted CHK files under the data root. Campaign folders are single
 * player. scenario.chk takes the map folder's name (the folder above
 * staredit/); any other .chk uses its own name. */
enum { SC_NET_MAPS = 64, SC_LISTED_GAMES = 32 };
typedef struct {
    char path[512], filename[128], title[128], description[512];
    int players, width, height, tileset;
    uint8_t race[8], side[8];
} sc_netmap_t;
static sc_netmap_t net_maps[SC_NET_MAPS];
static int net_map_count, chosen_map = -1, listed_count;
static char session_title[32], session_path[512], status_note[256];
static char chat_log[NETCHAT_LENGTH * 33 + 8];
static const char no_games[]="No games found. Create Game hosts one.";
static const char no_maps[]="No multiplayer maps found.";
static netgame_t listed[SC_LISTED_GAMES];
/* gluall.tbl 101. The connection row is the retail LAN entry; the session is TCP. */
static const char lan_prose[] =
    "Local-area networks are commonly used to connect office computers together, but home "
    "networks can also be constructed relatively inexpensively. Players connected to a "
    "local-area network can play against any other players connected to the same network.";

static int prefixed_players(const char *name) {
    if (name && name[0] == '(' && name[1] >= '2' && name[1] <= '8' && name[2] == ')') return name[1] - '0';
    return 0;
}

static bool chk_name(const char *name) {
    size_t n = strlen(name);
    if (n < 4 || name[n - 4] != '.') return false;
    char ext[4] = {name[n - 3], name[n - 2], name[n - 1], 0};
    for (int i = 0; ext[i]; ++i) if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = (char)(ext[i] - 'A' + 'a');
    return !strcmp(ext, "chk");
}

/* SIDE 0 is Zerg, 2 is Protoss. User-select, random and anything else stay Terran. */
static int lobby_race(unsigned side) {
    if (side == 0) return 1;
    if (side == 2) return 2;
    return 0;
}

static const char *race_name(int race) {
    return race == 1 ? "Zerg" : race == 2 ? "Protoss" : "Terran";
}

/* STR offsets are from the start of the chunk, same as the mission tip reader. */
static void copy_chk_string(const uint8_t *strings, size_t chunk, unsigned id, char *out, size_t size) {
    if (!out || !size) return;
    out[0] = '\0';
    if (!strings || chunk < 2 || !id) return;
    unsigned count = read_u16_le(strings);
    if (count > (chunk - 2) / 2 || id > count) return;
    size_t table = 2u + (size_t)count * 2u;
    size_t offset = read_u16_le(strings + (size_t)id * 2u);
    if (offset < table || offset >= chunk) return;
    size_t n = 0;
    for (size_t i = offset; i < chunk && strings[i] && n + 1 < size; ++i) {
        unsigned char ch = strings[i];
        if (ch == '\r') continue;
        if (ch == '\n' || ch >= 32) out[n++] = (char)ch;
    }
    out[n] = '\0';
}

static void map_title(const char *rel, const char *file, char *out, size_t size);

/* One pass for the create screen. Playable OWNR slots, in order, become seats. */
static bool fill_map(const char *path, const char *file, const char *rel, sc_netmap_t *map) {
    memset(map, 0, sizeof(*map));
    map->tileset = -1;
    char folder[128];
    map_title(rel, file, folder, sizeof(folder));
    snprintf(map->filename, sizeof(map->filename), "%s", !strcasecmp(file, "scenario.chk") ? folder : file);
    snprintf(map->title, sizeof(map->title), "%s", folder);
    blob_t blob = {0};
    const uint8_t *ownr = NULL, *side = NULL, *dim = NULL, *era = NULL, *sprp = NULL, *str = NULL;
    size_t ownr_n = 0, side_n = 0, dim_n = 0, era_n = 0, sprp_n = 0, str_n = 0;
    if (W_ReadFile(path, &blob)) {
        for (size_t at = 0; at + 8 <= blob.size;) {
            size_t n = read_u32_le(blob.bytes + at + 4);
            if (n > blob.size - at - 8) break;
            const uint8_t *tag = blob.bytes + at, *data = tag + 8;
            if (!ownr && !memcmp(tag, "OWNR", 4)) { ownr = data; ownr_n = n; }
            else if (!side && !memcmp(tag, "SIDE", 4)) { side = data; side_n = n; }
            else if (!dim && !memcmp(tag, "DIM ", 4)) { dim = data; dim_n = n; }
            else if (!era && !memcmp(tag, "ERA ", 4)) { era = data; era_n = n; }
            else if (!sprp && !memcmp(tag, "SPRP", 4)) { sprp = data; sprp_n = n; }
            else if (!str && !memcmp(tag, "STR ", 4)) { str = data; str_n = n; }
            at += 8 + n;
        }
        if (ownr && ownr_n >= 8) {
            int slot[8], counted = 0;
            for (int i = 0; i < 8; ++i)
                if (ownr[i] == 5 || ownr[i] == 6) slot[counted++] = i;
            if (counted >= 2) {
                map->players = counted > 8 ? 8 : counted;
                for (int i = 0; i < map->players; ++i) {
                    int seat = slot[i];
                    map->side[i] = (uint8_t)(side && (size_t)seat < side_n ? side[seat] : 5);
                    map->race[i] = (uint8_t)lobby_race(map->side[i]);
                }
            }
        }
        if (dim && dim_n >= 4) {
            map->width = read_u16_le(dim);
            map->height = read_u16_le(dim + 2);
        }
        if (era && era_n >= 2) map->tileset = read_u16_le(era) & 7;
        if (sprp && sprp_n >= 4 && str) {
            char title[128];
            copy_chk_string(str, str_n, read_u16_le(sprp), title, sizeof(title));
            if (title[0]) snprintf(map->title, sizeof(map->title), "%s", title);
            copy_chk_string(str, str_n, read_u16_le(sprp + 2), map->description, sizeof(map->description));
        }
        W_FreeFile(&blob);
    }
    if (map->players < 2) {
        map->players = prefixed_players(file);
        if (map->players < 2) map->players = prefixed_players(folder);
    }
    return map->players >= 2;
}

static void map_title(const char *rel, const char *file, char *out, size_t size) {
    if (!strcasecmp(file, "scenario.chk")) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s", rel);
        char *slash = strrchr(tmp, '/');
        if (slash) *slash = '\0';
        char *parent = strrchr(tmp, '/');
        const char *folder = parent ? parent + 1 : tmp;
        if (!strcasecmp(folder, "staredit") && parent) {
            *parent = '\0';
            parent = strrchr(tmp, '/');
            folder = parent ? parent + 1 : tmp;
        }
        snprintf(out, size, "%s", folder[0] ? folder : file);
        return;
    }
    snprintf(out, size, "%s", file);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static void scan_tree(const char *root, const char *rel) {
    char dir[1024];
    if (rel[0]) snprintf(dir, sizeof(dir), "%s/%s", root, rel);
    else snprintf(dir, sizeof(dir), "%s", root);
    DIR *listing = opendir(dir);
    if (!listing) return;
    struct dirent *entry;
    while ((entry = readdir(listing))) {
        if (entry->d_name[0] == '.' || !strcasecmp(entry->d_name, "campaign")) continue;
        char child[512];
        if (rel[0]) snprintf(child, sizeof(child), "%s/%s", rel, entry->d_name);
        else snprintf(child, sizeof(child), "%s", entry->d_name);
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", root, child);
        DIR *sub = opendir(full);
        if (sub) { closedir(sub); scan_tree(root, child); continue; }
        if (net_map_count >= SC_NET_MAPS || !chk_name(entry->d_name)) continue;
        sc_netmap_t *map = &net_maps[net_map_count];
        if (!fill_map(full, entry->d_name, child, map)) continue;
        snprintf(map->path, sizeof(map->path), "%s", child);
        ++net_map_count;
    }
    closedir(listing);
}

static int compare_net_maps(const void *a, const void *b) {
    return strcasecmp(((const sc_netmap_t *)a)->filename, ((const sc_netmap_t *)b)->filename);
}

static void scan_net_maps(const char *root) {
    net_map_count = 0;
    if (root && root[0]) scan_tree(root, "");
    if (net_map_count) qsort(net_maps, (size_t)net_map_count, sizeof(net_maps[0]), compare_net_maps);
}

static void net_commit(void) {
    int races[MAXPLAYERS];
    for (int i = 0; i < MAXPLAYERS; ++i) races[i] = M_NetPlayerRace(i);
    sc_set_net_races(races);
}

static const char *chat_name(void) { return player_name[0] ? player_name : "Player"; }

static void use_net(void) {
    netplay_t net = {
        .max_players = 8, .race_count = 3, .joiner_fields = NET_FIELD_RACE,
        .commit = net_commit, .chat_name = chat_name,
    };
    M_NetUse(&net);
}

static void set_text(menuitem_t *item, const char *text) {
    if (!item) return;
    item->prose = NULL;
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
}

static void set_prose(menuitem_t *item, const char *text) {
    if (!item) return;
    item->text[0] = '\0';
    item->prose = text ? text : "";
}

static void as_list(menuitem_t *item, int rows, int value, const char *(*row)(const menuitem_t *, int)) {
    if (!item) return;
    item->kind = MI_LIST;
    item->rows = rows;
    item->value = value;
    item->first_row = 0;
    item->row = row;
    item->row_height = item->font->line_h;
    item->align = 0;
    item->enabled = item->visible = true;
}

static void as_drop(menuitem_t *item, int rows, const char *(*row)(const menuitem_t *, int)) {
    if (!item) return;
    item->kind = MI_DROPDOWN;
    item->rows = rows;
    item->value = 0;
    item->row = row;
    item->row_height = item->font->line_h;
    item->inset = (ivec2_t){4, 0};
    item->align = MALIGN_VCENTER;
    item->look[MS_NORMAL].palette = 1;
    item->enabled = item->visible = true;
    snprintf(item->text, sizeof(item->text), "%s", row(item, 0));
}

static const char *conn_row(const menuitem_t *item, int row) {
    (void)item; (void)row; return "IPX network";
}
static const char *map_row(const menuitem_t *item, int row) {
    (void)item; return row >= 0 && row < net_map_count ? net_maps[row].filename : "";
}
static const char *game_row(const menuitem_t *item, int row) {
    (void)item; return row >= 0 && row < listed_count ? listed[row].name : "";
}
static const char *type_row(const menuitem_t *item, int row) {
    (void)item; (void)row; return "Melee";
}
/* gluall.tbl 129, 130: what the host makes of a seat nobody has taken. */
static const char *seat_row(const menuitem_t *item, int row) {
    (void)item; return row ? "Closed" : "Open";
}
static const char *race_row(const menuitem_t *item, int row) {
    (void)item; return race_name(row);
}

/* gluall.tbl 81..87. StarCraft's frames last 167, 111, 83, 67, 56, 48 and
 * 42 ms; against Normal those are the engine's 40..160 percent. */
enum { SC_SPEEDS = 7 };
static const char *const speed_names[SC_SPEEDS] = {
    "Slowest", "Slower", "Slow", "Normal", "Fast", "Faster", "Fastest",
};
static int speed_percent(int index) { return 40 + index * 20; }
static int speed_index(int percent) {
    int index = (percent - 30) / 20;
    return index < 0 ? 0 : index >= SC_SPEEDS ? SC_SPEEDS - 1 : index;
}
static const char *speed_text(int percent) {
    return speed_percent(speed_index(percent)) == percent ? speed_names[speed_index(percent)] : M_va("%d%%", percent);
}
/* gluall.tbl 38..42 name the original five; Brood War's ERA 5..7 follow. */
static const char *tileset_name(int era) {
    static const char *const names[] = {
        "Badlands", "Space", "Installation", "Ashworld", "Jungle", "Desert", "Ice", "Twilight",
    };
    return era >= 0 && era < 8 ? names[era] : "";
}

static int map_index(const char *path) {
    if (!path) return -1;
    for (int i = 0; i < net_map_count; ++i)
        if (!strcmp(net_maps[i].path, path)) return i;
    return -1;
}

static const char *path_title(const char *path) {
    static char titled[128];
    titled[0] = '\0';
    if (!path || !path[0]) return titled;
    const char *slash = strrchr(path, '/');
    map_title(path, slash ? slash + 1 : path, titled, sizeof(titled));
    return titled;
}

static void show_screen(screen_t *screen);
static void show_conn(void);
static void show_join(void);
static void show_create(void);
static void host_game(void);
static void paint_listed(void);
static void paint_create(void);
static void multi_escape(menu_t *menu);
static void multi_tick(menu_t *menu);

static void size_text(menuitem_t *item, int width, int height) {
    if (width > 0 && height > 0) set_text(item, M_va("%ux%u", (unsigned)width, (unsigned)height));
    else set_text(item, "");
}

static void paint_listed(void) {
    set_text(M_MenuFind(&join.menu,6),status_note);
    menuitem_t *list = M_MenuFind(&join.menu, 5);
    int row = list ? list->value : -1;
    M_MenuFind(&join.menu,13)->enabled=row>=0&&row<listed_count;
    set_text(M_MenuFind(&join.menu, 9), "");
    /* Discovery does not include the host's speed; the lobby handshake does. */
    set_text(M_MenuFind(&join.menu, 12), "");
    if (row < 0 || row >= listed_count) {
        set_text(M_MenuFind(&join.menu, 7), "");
        set_text(M_MenuFind(&join.menu, 8), "");
        set_text(M_MenuFind(&join.menu, 10), "");
        set_text(M_MenuFind(&join.menu, 11), "");
        return;
    }
    const netgame_t *game = &listed[row];
    int index = map_index(game->map);
    set_text(M_MenuFind(&join.menu, 7), game->name);
    set_text(M_MenuFind(&join.menu, 8), "Melee");
    if (index >= 0) {
        set_text(M_MenuFind(&join.menu, 10), net_maps[index].title);
        size_text(M_MenuFind(&join.menu, 11), net_maps[index].width, net_maps[index].height);
    } else {
        set_text(M_MenuFind(&join.menu, 10), path_title(game->map));
        set_text(M_MenuFind(&join.menu, 11), "");
    }
}

/* glucreat's info rows print gluall.tbl 30..37, "label%cvalue": the value
 * sits at the row's right edge. Each value is its label's id plus 100. */
static void screen_row(screen_t *screen, int id, const char *label, const char *value) {
    set_text(M_MenuFind(&screen->menu, id), value ? label : "");
    set_text(M_MenuFind(&screen->menu, id + 100), value);
}
static void info_row(int id, const char *label, const char *value) { screen_row(&create, id, label, value); }

static void paint_create(void) {
    menuitem_t *list = M_MenuFind(&create.menu, 5);
    int row = list ? list->value : -1;
    const sc_netmap_t *map = row >= 0 && row < net_map_count ? &net_maps[row] : NULL;
    M_MenuFind(&create.menu, 12)->enabled = map != NULL;
    set_text(M_MenuFind(&create.menu, 7), map ? map->title : "");
    set_prose(M_MenuFind(&create.menu, 8), map ? map->description : "");
    info_row(10, "Map Size:", map && map->width > 0 ? M_va("%dx%d", map->width, map->height) : NULL);
    info_row(11, "Tileset:", map && map->tileset >= 0 ? tileset_name(map->tileset) : NULL);
    info_row(9, "Number of Players:", map ? M_va("%d", map->players) : NULL);
    /* gluall.tbl 89. The slider's row has no setting in a melee game. */
    menuitem_t *speed = M_MenuFind(&create.menu, 16);
    set_text(M_MenuFind(&create.menu, 15), M_va("Speed: %s", speed_names[speed->value]));
}

static void scroll_log(menuitem_t *item) {
    if (!item || !item->font || item->font->line_h < 1) return;
    int height = V_TextWrappedHeight(item->rect.w, item->font, item->prose ? item->prose : "");
    int lines = (height + item->font->line_h - 1) / item->font->line_h;
    int visible = item->rect.h / item->font->line_h;
    M_MenuSetRows(item,lines);
    item->first_row = lines > visible ? lines - visible : 0;
}

static void join_refresh(menu_t *menu) {
    fit_scrollbars(menu);
    if (I_NetJoining()) return;
    menuitem_t *list = M_MenuFind(&join.menu, 5);
    if (!list) return;
    int count = 0;
    const netgame_t *games = I_NetGames(&count);
    if (count > SC_LISTED_GAMES) count = SC_LISTED_GAMES;
    listed_count = count;
    if (listed_count > 0 && games) memcpy(listed, games, (size_t)listed_count * sizeof(listed[0]));
    M_MenuSetRows(list, listed_count);
    list->prose=list->rows?NULL:no_games;
    if (list->value < 0 || list->value >= listed_count) list->value = listed_count ? 0 : -1;
    paint_listed();
}

static int closed_seats(uint8_t mask) {
    int n = 0;
    for (; mask; mask &= mask - 1) ++n;
    return n;
}

/* Lobby rows are the map's seats. Option byte 0 marks the rows the host
 * closed; the players fill the others in order. */
static int row_player[8];

static void chat_refresh(menu_t *menu) {
    const char *live = I_NetMap();
    if (live && live[0] && strcmp(live, session_path)) {
        snprintf(session_path, sizeof(session_path), "%s", live);
        chosen_map = map_index(session_path);
    }
    const sc_netmap_t *map = chosen_map >= 0 && chosen_map < net_map_count ? &net_maps[chosen_map] : NULL;
    set_text(M_MenuFind(&chat.menu, 14), session_title);
    set_text(M_MenuFind(&chat.menu, 15), "Melee");
    set_text(M_MenuFind(&chat.menu, 16), "");
    set_text(M_MenuFind(&chat.menu, 17), map ? map->title : path_title(session_path));
    if (map) size_text(M_MenuFind(&chat.menu, 18), map->width, map->height);
    else set_text(M_MenuFind(&chat.menu, 18), "");
    set_text(M_MenuFind(&chat.menu, 19), speed_text(game_speed));
    menuitem_t *log = M_MenuFind(&chat.menu, 10);
    const char *text=M_NetLog();
    if(strcmp(chat_log,text)) {
        snprintf(chat_log,sizeof(chat_log),"%s",text);
        scroll_log(log);
    }
    uint8_t closed = 0;
    M_NetOptions(&closed, 1);
    int rows = doomcom && doomcom->numplayers > 0 ? doomcom->numplayers + closed_seats(closed) : 0;
    if (rows > 8) rows = 8;
    int joined = I_NetPlayerCount(), local = M_NetLocalSlot();
    for (int i = 0, player = 0; i < 8; ++i) {
        menuitem_t *name = M_MenuFind(&chat.menu, 28 + i * 4);
        menuitem_t *race = M_MenuFind(&chat.menu, 29 + i * 4);
        bool on = i < rows, shut = on && (closed >> i & 1);
        int p = row_player[i] = on && !shut ? player++ : -1;
        name->visible = on;
        race->visible = p >= 0;
        if (!on) continue;
        const netseat_t *seat = p >= 0 ? M_NetSeat(p) : NULL;
        bool filled = p >= 0 && p < joined;
        /* The host opens and closes the seats nobody has taken. */
        bool choice = M_NetHosting() && i > 0 && !filled;
        name->kind = choice ? MI_DROPDOWN : MI_STATIC;
        name->enabled = choice;
        name->value = shut;
        name->inset = (ivec2_t){choice ? 4 : 0, 0};
        name->align = choice ? MALIGN_VCENTER : 0;
        if (shut) set_text(name, seat_row(name, 1));
        else if (!filled) set_text(name, seat_row(name, 0));
        else if (p == local) set_text(name, chat_name());
        else set_text(name, M_va("Player %d", p + 1));
        name->look[MS_NORMAL].palette = choice || (filled && seat->ready) ? 1 : 0;
        if (!seat) continue;
        race->value = seat->race;
        set_text(race, race_name(seat->race));
        race->enabled = filled && p == local && !seat->ready;
        race->hotkey = 0;
    }
    const netseat_t *mine = M_NetSeat(local);
    set_text(M_MenuFind(&chat.menu, 6), mine && mine->ready ? "Unready" : "Ready");
    fit_scrollbars(menu);
}

static void show_screen(screen_t *screen) {
    screen->menu.escape = multi_escape;
    screen->menu.ticker = screen == &join || screen == &chat ? multi_tick : NULL;
    screen->menu.refresh = screen == &join ? join_refresh : screen == &chat ? chat_refresh : fit_scrollbars;
    screen->menu.app = front.app;
    screen->menu.refresh(&screen->menu);
    M_SetupNextMenu(&screen->menu);
}

static void show_conn(void) {
    set_text(M_MenuFind(&conn.menu, 6), "IPX network");
    set_text(M_MenuFind(&conn.menu, 7), "Supports up to 8 players");
    set_prose(M_MenuFind(&conn.menu, 8), lan_prose);
    show_screen(&conn);
}

static void show_join(void) {
    scan_net_maps(asset_root);
    if (!M_NetBrowse()) snprintf(status_note, sizeof(status_note), "%s", M_NetNotice());
    else status_note[0] = '\0';
    show_screen(&join);
}

static void show_create(void) {
    scan_net_maps(asset_root);
    menuitem_t *list = M_MenuFind(&create.menu, 5);
    M_MenuSetRows(list, net_map_count);
    list->prose = net_map_count ? NULL : no_maps;
    if (list->value < 0 || list->value >= net_map_count) list->value = net_map_count ? 0 : -1;
    M_MenuFind(&create.menu, 16)->value = speed_index(game_speed);
    set_text(M_MenuFind(&create.menu, 6), "");
    paint_create();
    show_screen(&create);
}

static void fail_join(void) {
    char note[256];
    snprintf(note, sizeof(note), "%s", M_NetNotice());
    show_join();
    if (!note[0]) return;
    snprintf(status_note, sizeof(status_note), "%s", note);
    set_text(M_MenuFind(&join.menu, 6), status_note);
}

/* Every seat the map has starts open; the host closes some in the lobby. */
static void host_game(void) {
    menuitem_t *selected = M_MenuFind(&create.menu, 5);
    chosen_map = selected ? selected->value : -1;
    if (chosen_map < 0 || chosen_map >= net_map_count) return;
    const sc_netmap_t *map = &net_maps[chosen_map];
    snprintf(session_title, sizeof(session_title), "%.31s", map->title);
    snprintf(session_path, sizeof(session_path), "%s", map->path);
    for (int i = 0; i < MAXPLAYERS; ++i) {
        netseat_t seat = {
            .race = (uint8_t)(i < map->players ? map->race[i] : 0),
            .type = 1, .color = (uint8_t)i, .team = (uint8_t)i,
        };
        M_NetSetSeat(i, &seat);
    }
    if (!M_NetHost(session_title, session_path, map->players)) {
        set_text(M_MenuFind(&create.menu, 6), M_NetNotice());
        return;
    }
    status_note[0] = '\0';
    show_screen(&chat);
    menuitem_t *field = M_MenuFind(&chat.menu, 9);
    if (field) chat.menu.itemOn = (int)(field - chat.items);
}

static void multi_escape(menu_t *menu) {
    if (menu == &conn.menu) {
        M_NetStop();
        M_SetupNextMenu(&front);
    } else if (menu == &join.menu) {
        M_NetStop();
        show_conn();
    } else if (menu == &create.menu) {
        show_join();
    } else if (menu == &chat.menu) {
        M_NetStop();
        status_note[0] = '\0';
        show_join();
    }
}

static void multi_tick(menu_t *menu) {
    M_MenuTicker(menu);
    if (menu == &join.menu) {
        if (!I_NetJoining()) return;
        int result = M_NetPoll();
        if (result < 0) fail_join();
        else if (result > 0) M_ClearMenus();
        else if (M_NetInLobby()) {
            status_note[0] = '\0';
            show_screen(&chat);
            menuitem_t *field = M_MenuFind(&chat.menu, 9);
            if (field) chat.menu.itemOn = (int)(field - chat.items);
        }
    } else if (menu == &chat.menu) {
        int result = M_NetPoll();
        if (result < 0) {
            fail_join();
        } else if (result > 0) M_ClearMenus();
    }
}

static void join_game(void) {
    menuitem_t *list = M_MenuFind(&join.menu, 5);
    int row = list ? list->value : -1;
    if (row < 0 || row >= listed_count || !listed[row].address[0]) return;
    snprintf(session_title, sizeof(session_title), "%.31s", listed[row].name);
    snprintf(session_path, sizeof(session_path), "%s", listed[row].map);
    chosen_map = map_index(session_path);
    if (!M_NetJoinAddress(listed[row].address)) {
        snprintf(status_note, sizeof(status_note), "%s", M_NetNotice());
        set_text(M_MenuFind(&join.menu, 6), status_note);
        return;
    }
    status_note[0] = '\0';
    set_text(M_MenuFind(&join.menu, 6), "");
}

static void send_chat(void) {
    menuitem_t *field = M_MenuFind(&chat.menu, 9);
    if (!field || !field->text[0]) return;
    if (M_NetChat(field->text)) field->text[0] = '\0';
}

/* A closed row leaves the session one seat smaller. Seats already taken
 * stay, so the host cannot close below the players who have joined. */
static void close_seat(int row, bool shut) {
    uint8_t closed = 0;
    M_NetOptions(&closed, 1);
    int rows = doomcom->numplayers + closed_seats(closed);
    uint8_t next = shut ? closed | 1u << row : closed & ~(1u << row);
    if (next != closed && M_NetSetPlayers(rows - closed_seats(next))) M_NetSetOptions(&next, 1);
}

static void multi(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (menu == &join.menu && item->id == 5 && action == MA_CHANGE) {
        status_note[0] = '\0';
        paint_listed();
        return;
    }
    if (menu == &create.menu && action == MA_CHANGE) {
        if (item->id == 16) D_SetGameSpeed(speed_percent(item->value));
        set_text(M_MenuFind(&create.menu, 6), "");
        paint_create();
        return;
    }
    if (menu == &chat.menu && action == MA_CHANGE && item->id >= 28 && item->id <= 59) {
        int row = (item->id - 28) / 4;
        if ((item->id - 28) % 4 == 0 && row > 0 && M_NetHosting()) close_seat(row, item->value != 0);
        int slot = row_player[row];
        const netseat_t *seat = M_NetSeat(slot);
        if ((item->id - 28) % 4 == 1 && slot == M_NetLocalSlot() && seat && !seat->ready) {
            netseat_t choice = *seat;
            choice.race = (uint8_t)item->value;
            M_NetSetSeat(slot, &choice);
        }
        chat_refresh(menu);
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (menu == &conn.menu) {
        if (item->id == 9) show_join();
        else if (item->id == 10) multi_escape(menu);
    } else if (menu == &join.menu) {
        if (item->id == 13 || item->id == 5) join_game();
        else if (item->id == 15) show_create();
        else if (item->id == 14) multi_escape(menu);
    } else if (menu == &create.menu) {
        if (item->id == 12 || item->id == 5) host_game();
        else if (item->id == 13) multi_escape(menu);
    } else if (menu == &chat.menu) {
        if (item->id == 6) M_NetToggleReady();
        else if (item->id == 8 || item->id == 9) send_chat();
        else if (item->id == 7) multi_escape(menu);
        chat_refresh(menu);
    }
}

static void open_multi(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    M_NetStop();
    use_net();
    if (menu && menu->app) front.app = menu->app;
    show_conn();
}

static bool add_value(screen_t *screen, int id) {
    menuitem_t *label = M_MenuFind(&screen->menu, id);
    if (!label || screen->menu.numitems == SC_DIALOG_CONTROLS) return false;
    menuitem_t *value = &screen->items[screen->menu.numitems++];
    *value = *label;
    value->id = id + 100;
    value->align = MALIGN_RIGHT;
    value->userdata = NULL;
    value->text[0] = '\0';
    return true;
}

static bool wire_multi(void) {
    menuitem_t *list = M_MenuFind(&conn.menu, 5);
    if (!list || !M_MenuFind(&conn.menu, 9) || !M_MenuFind(&join.menu, 5) ||
        !M_MenuFind(&join.menu,13) || !M_MenuFind(&join.menu,15) ||
        !M_MenuFind(&create.menu, 5) || !M_MenuFind(&create.menu, 12) || !M_MenuFind(&create.menu, 14) ||
        !M_MenuFind(&create.menu, 15) || !M_MenuFind(&create.menu, 16) || !M_MenuFind(&create.menu, 17) ||
        !M_MenuFind(&create.menu, 18) || !M_MenuFind(&chat.menu, 8) ||
        !M_MenuFind(&chat.menu, 9) || !M_MenuFind(&chat.menu, 10))
        return false;
    as_list(list, 1, 0, conn_row);
    as_list(M_MenuFind(&join.menu, 5), 0, -1, game_row);
    M_MenuFind(&join.menu,5)->prose=no_games;
    menuitem_t *ok = M_MenuFind(&join.menu, 13);
    if (ok) { ok->enabled = false; ok->disabled_look = true; }
    M_MenuFind(&join.menu,15)->disabled_look=true;
    as_list(M_MenuFind(&create.menu, 5), 0, -1, map_row);
    as_drop(M_MenuFind(&create.menu, 17), 1, type_row);
    /* Melee has no subtype. */
    M_MenuFind(&create.menu, 14)->visible = false;
    M_MenuFind(&create.menu, 18)->visible = false;
    menuitem_t *speed = M_MenuFind(&create.menu, 16);
    speed->range.min = 0;
    speed->range.max = SC_SPEEDS - 1;
    if (!add_value(&create, 9) || !add_value(&create, 10) || !add_value(&create, 11)) return false;
    /* gluchat's Send is a 1x1 button at the field's end: Enter, not a label. */
    M_MenuFind(&chat.menu, 8)->text[0] = '\0';
    menuitem_t *field = M_MenuFind(&chat.menu, 9);
    field->kind = MI_TEXTFIELD;
    field->maxchars = 60;
    field->enabled = field->visible = true;
    menuitem_t *log = M_MenuFind(&chat.menu, 10);
    as_list(log,0,-1,NULL);
    log->row_height=log->font->line_h;
    log->prose = chat_log;
    for (int i = 0; i < 8; ++i) {
        menuitem_t *name = M_MenuFind(&chat.menu, 28 + i * 4);
        menuitem_t *race = M_MenuFind(&chat.menu, 29 + i * 4);
        if (!name || !race) return false;
        as_drop(name, 2, seat_row);
        name->kind = MI_STATIC;
        name->hotkey = 0;
        name->release = true;
        as_drop(race, 3, race_row);
        race->hotkey = 0;
        race->release = true;
        race->visible = name->visible = false;
        race->enabled = name->enabled = false;
    }
    return true;
}

/* ── popups, Load Saved and Play Custom ─────────────────────────────────── */

/* glupok and glupokcancel over the screen that asked, in that screen's
 * palette, on the palette folder's pOPopup and pDPopup pictures. */
static char notice_text[512];
static void (*confirmed)(void);

static void popup_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE || (item->id != 1 && item->id != 3)) return;
    void (*then)(void) = menu == &confirm.menu && item->id == 1 ? confirmed : NULL;
    M_SetupNextMenu(beneath);
    if (then) then();
}

static void popup_escape(menu_t *menu) { (void)menu; M_SetupNextMenu(beneath); }

static void free_art(screen_t *screen) {
    for (int i = 0; i < SC_DIALOG_CONTROLS; i++) {
        R_FreeSprite(&screen->art[i].image);
        for (int j = 0; j < SC_CONTROL_MOVIES; j++) {
            R_FreeSprite(&screen->art[i].movies[j].sheet);
            free(screen->art[i].movies[j].palettes);
        }
    }
    memset(screen, 0, sizeof(*screen));
}

static void popup(screen_t *screen, const char *dialog, const char *picture, const char *text, void (*yes)(void)) {
    menu_t *under = currentmenu;
    theme_t *theme = under && under != &notice.menu && under != &confirm.menu ? under->owner : NULL;
    if (!theme) { M_StartMessage(text); return; }
    if (screen->theme != theme) {
        char palette[160];
        snprintf(palette, sizeof(palette), "%s/backgnd.pcx", theme->path);
        free_art(screen);
        if (!load_popup(screen, under->app, asset_root, dialog, palette, picture, popup_action)) {
            free_art(screen);
            M_StartMessage(text);
            return;
        }
        screen->menu.background = NULL;
        screen->menu.escape = popup_escape;
        set_prose(M_MenuFind(&screen->menu, 2), notice_text);
    }
    snprintf(notice_text, sizeof(notice_text), "%s", text);
    beneath = under;
    confirmed = yes;
    screen->menu.app = under->app;
    menuitem_t *ok = M_MenuFind(&screen->menu, 1);
    if (ok) screen->menu.itemOn = (int)(ok - screen->items);
    M_SetupNextMenu(&screen->menu);
}

static void show_notice(const char *text) { popup(&notice, "rez/glupok.bin", "popopup.pcx", text, NULL); }
static void ask(const char *text, void (*yes)(void)) {
    popup(&confirm, "rez/glupokcancel.bin", "pdpopup.pcx", text, yes);
}

/* gluload lists the engine's saved games and any retail .snx in save/. */
enum { SC_SAVES = 64 };
typedef struct { char path[1024], name[64]; } sc_save_t;
static sc_save_t saves[SC_SAVES];
static int save_count;
static const char no_saves[] = "No saved games found.";

static void scan_save_dir(const char *dir, const char *ext) {
    DIR *listing = dir && dir[0] ? opendir(dir) : NULL;
    if (!listing) return;
    struct dirent *entry;
    while ((entry = readdir(listing)) && save_count < SC_SAVES) {
        size_t n = strlen(entry->d_name);
        if (n <= 4 || strcasecmp(entry->d_name + n - 4, ext)) continue;
        sc_save_t *save = &saves[save_count];
        M_PathJoin(save->path, sizeof(save->path), dir, entry->d_name);
        saveinfo_t info;
        if (!strcasecmp(ext, ".sav")) {
            if (!G_SaveInfo(save->path, &info)) continue;
            snprintf(save->name, sizeof(save->name), "%s", info.name);
        } else snprintf(save->name, sizeof(save->name), "%.*s", (int)(n - 4), entry->d_name);
        ++save_count;
    }
    closedir(listing);
}

static int compare_saves(const void *a, const void *b) {
    return strcasecmp(((const sc_save_t *)a)->name, ((const sc_save_t *)b)->name);
}

static const char *save_row(const menuitem_t *item, int row) {
    (void)item; return row >= 0 && row < save_count ? saves[row].name : "";
}

static void load_refresh(menu_t *menu) {
    menuitem_t *list = M_MenuFind(menu, 6);
    bool chosen = list->value >= 0 && list->value < save_count;
    M_MenuFind(menu, 4)->enabled = M_MenuFind(menu, 7)->enabled = chosen;
    fit_scrollbars(menu);
}

static void fill_saves(void) {
    save_count = 0;
    scan_save_dir(D_UserDirectory(), ".sav");
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/save", asset_root);
    scan_save_dir(dir, ".snx");
    qsort(saves, (size_t)save_count, sizeof(saves[0]), compare_saves);
    menuitem_t *list = M_MenuFind(&load_game.menu, 6);
    M_MenuSetRows(list, save_count);
    list->prose = save_count ? NULL : no_saves;
    if (list->value < 0 || list->value >= save_count) list->value = save_count ? 0 : -1;
}

static void show_load(void) {
    fill_saves();
    menuitem_t *list = M_MenuFind(&load_game.menu, 6);
    load_game.menu.itemOn = (int)(list - load_game.items);
    load_game.menu.app = front.app;
    load_refresh(&load_game.menu);
    M_SetupNextMenu(&load_game.menu);
}

static void delete_save(void) {
    int row = M_MenuFind(&load_game.menu, 6)->value;
    if (row < 0 || row >= save_count) return;
    if (remove(saves[row].path)) show_notice("The saved game could not be deleted.");
    fill_saves();
}

static void load_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    int row = M_MenuFind(menu, 6)->value;
    bool chosen = row >= 0 && row < save_count;
    if (item->id == 5) single_escape(menu);
    /* gluall.tbl 22. */
    else if (item->id == 7 && chosen) ask("Delete this saved game?", delete_save);
    else if ((item->id == 4 || item->id == 6) && chosen)
        show_notice("Loading saved games is not available yet.");
}

/* glucustm: the first playable slot is the player; the others are Computer
 * or Closed (gluall.tbl 129, 131). Race choices end in Random. */
static int custom_kinds[8], custom_races[8];
static const char *custom_seat_row(const menuitem_t *item, int row) {
    (void)item; return row ? "Closed" : "Computer";
}
static const char *custom_race_row(const menuitem_t *item, int row) {
    (void)item; return row == 3 ? "Random" : race_name(row);
}

static const sc_netmap_t *custom_choice(void) {
    int row = M_MenuFind(&custom.menu, 5)->value;
    return row >= 0 && row < net_map_count ? &net_maps[row] : NULL;
}

/* A map that fixes a slot's race keeps it; user-select slots start as
 * Terran for the player and Random for the computers. */
static void reset_custom(const sc_netmap_t *map) {
    for (int i = 0; i < 8; ++i) {
        unsigned side = map ? map->side[i] : 5;
        custom_kinds[i] = i ? SC_SLOT_COMPUTER : SC_SLOT_HUMAN;
        custom_races[i] = side <= 2 ? lobby_race(side) : i ? 3 : 0;
    }
}

static void paint_custom(void) {
    const sc_netmap_t *map = custom_choice();
    M_MenuFind(&custom.menu, 12)->enabled = map != NULL;
    set_text(M_MenuFind(&custom.menu, 7), map ? map->title : "");
    set_prose(M_MenuFind(&custom.menu, 8), map ? map->description : "");
    screen_row(&custom, 10, "Map Size:", map && map->width > 0 ? M_va("%dx%d", map->width, map->height) : NULL);
    screen_row(&custom, 11, "Tileset:", map && map->tileset >= 0 ? tileset_name(map->tileset) : NULL);
    screen_row(&custom, 37, "Computer Slots:", map ? M_va("%d", map->players - 1) : NULL);
    screen_row(&custom, 36, "Human Slots:", map ? "1" : NULL);
    for (int i = 0; i < 8; ++i) {
        menuitem_t *name = M_MenuFind(&custom.menu, 20 + i), *race = M_MenuFind(&custom.menu, 28 + i);
        bool on = map && i < map->players;
        name->visible = on;
        race->visible = race->enabled = on && custom_kinds[i] != SC_SLOT_CLOSED;
        race->value = custom_races[i];
        set_text(race, custom_race_row(race, custom_races[i]));
        name->kind = i ? MI_DROPDOWN : MI_STATIC;
        name->enabled = on && i;
        if (i) {
            name->value = custom_kinds[i] == SC_SLOT_CLOSED;
            set_text(name, custom_seat_row(name, name->value));
        } else set_text(name, chat_name());
    }
}

static void custom_refresh(menu_t *menu) {
    paint_custom();
    fit_scrollbars(menu);
}

static void show_custom(void) {
    scan_net_maps(asset_root);
    menuitem_t *list = M_MenuFind(&custom.menu, 5);
    M_MenuSetRows(list, net_map_count);
    list->prose = net_map_count ? NULL : no_maps;
    if (list->value < 0 || list->value >= net_map_count) list->value = net_map_count ? 0 : -1;
    reset_custom(custom_choice());
    custom.menu.itemOn = (int)(list - custom.items);
    custom.menu.app = front.app;
    custom_refresh(&custom.menu);
    M_SetupNextMenu(&custom.menu);
}

static void start_custom(void) {
    const sc_netmap_t *map = custom_choice();
    if (!map) return;
    int kinds[8], races[8], computers = 0;
    for (int i = 0; i < 8; ++i) {
        kinds[i] = i < map->players ? custom_kinds[i] : SC_SLOT_CLOSED;
        races[i] = custom_races[i] == 3 ? rand() % 3 : custom_races[i];
        computers += kinds[i] == SC_SLOT_COMPUTER;
    }
    /* gluall.tbl 54. */
    if (!computers) { show_notice("You must have at least one computer opponent."); return; }
    sc_set_net_races(NULL);
    sc_set_custom_slots(kinds, races);
    snprintf(custom_map, sizeof(custom_map), "%s", map->path);
    menumap = custom_map;
    M_ClearMenus();
}

static void custom_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action == MA_CHANGE) {
        if (item->id == 5) reset_custom(custom_choice());
        else if (item->id >= 21 && item->id <= 27)
            custom_kinds[item->id - 20] = item->value ? SC_SLOT_CLOSED : SC_SLOT_COMPUTER;
        else if (item->id >= 28 && item->id <= 35) custom_races[item->id - 28] = item->value;
        paint_custom();
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (item->id == 12 || item->id == 5) start_custom();
    else if (item->id == 13) single_escape(menu);
}

static bool wire_single(void) {
    menuitem_t *list = M_MenuFind(&load_game.menu, 6);
    if (!list || !M_MenuFind(&load_game.menu, 4) || !M_MenuFind(&load_game.menu, 7) ||
        !M_MenuFind(&custom.menu, 5) || !M_MenuFind(&custom.menu, 12) || !M_MenuFind(&custom.menu, 17))
        return false;
    as_list(list, 0, -1, save_row);
    list->prose = no_saves;
    load_game.menu.refresh = load_refresh;
    M_MenuFind(&load_game.menu, 4)->hotkey = SDLK_RETURN;
    as_list(M_MenuFind(&custom.menu, 5), 0, -1, map_row);
    /* The map browser is one flat list of every installed map. */
    set_text(M_MenuFind(&custom.menu, 6), "Maps");
    as_drop(M_MenuFind(&custom.menu, 17), 1, type_row);
    M_MenuFind(&custom.menu, 14)->visible = M_MenuFind(&custom.menu, 18)->visible = false;
    M_MenuFind(&custom.menu, 9)->visible = false;
    M_MenuFind(&custom.menu, 37)->visible = M_MenuFind(&custom.menu, 36)->visible = true;
    if (!add_value(&custom, 10) || !add_value(&custom, 11) || !add_value(&custom, 37) || !add_value(&custom, 36))
        return false;
    for (int i = 0; i < 8; ++i) {
        menuitem_t *name = M_MenuFind(&custom.menu, 20 + i), *race = M_MenuFind(&custom.menu, 28 + i);
        if (!name || !race) return false;
        as_drop(name, 2, custom_seat_row);
        as_drop(race, 4, custom_race_row);
        name->hotkey = race->hotkey = 0;
        name->visible = race->visible = false;
    }
    custom.menu.refresh = custom_refresh;
    return true;
}

/* ── in-level dialogs ───────────────────────────────────────────────────── */

/* Retail rez dialogs over the running world. Their routes follow the button
 * texts; Save, Load and the option sliders are not wired to anything yet. */
enum {
    IG_GAME, IG_OPTIONS, IG_SPEED, IG_SOUND, IG_VIDEO, IG_SAVE, IG_LOAD, IG_OBJECTIVES,
    IG_HELP_MENU, IG_HELP, IG_END, IG_RESTART, IG_QUIT_MISSION, IG_QUIT, IG_VICTORY, IG_DEFEAT, IG_SCREENS
};
static const struct { const char *dialog; int parent; } ingame_dialogs[IG_SCREENS] = {
    [IG_GAME]={"rez/gamemenu.bin",-1}, [IG_OPTIONS]={"rez/options.bin",IG_GAME},
    [IG_SPEED]={"rez/spd_dlg.bin",IG_OPTIONS}, [IG_SOUND]={"rez/snd_dlg.bin",IG_OPTIONS},
    [IG_VIDEO]={"rez/video.bin",IG_OPTIONS}, [IG_SAVE]={"rez/savegame.bin",IG_GAME},
    [IG_LOAD]={"rez/loadgame.bin",IG_GAME}, [IG_OBJECTIVES]={"rez/objctdlg.bin",IG_GAME},
    [IG_HELP_MENU]={"rez/helpmenu.bin",IG_GAME}, [IG_HELP]={"rez/help.bin",IG_HELP_MENU},
    [IG_END]={"rez/abrtmenu.bin",IG_GAME}, [IG_RESTART]={"rez/restart.bin",IG_END},
    [IG_QUIT_MISSION]={"rez/quit2mnu.bin",IG_END}, [IG_QUIT]={"rez/quit.bin",IG_END},
    [IG_VICTORY]={"rez/wmission.bin",-1}, [IG_DEFEAT]={"rez/lmission.bin",-1},
};
static screen_t ingame_screens[IG_SCREENS];
static char help_text[8192];

static int ingame_index(const menu_t *menu) {
    for(int i=0;i<IG_SCREENS;i++)if(menu==&ingame_screens[i].menu)return i;
    return -1;
}
static void show_ingame(int index) {
    screen_t *screen=&ingame_screens[index];
    if(!screen->menu.numitems){M_ClearMenus();return;}
    screen->menu.app=hud.app?hud.app:front.app;
    if(screen->menu.refresh)screen->menu.refresh(&screen->menu);
    M_SetupNextMenu(&screen->menu);
}
static void ingame_escape(menu_t *menu) {
    int index=ingame_index(menu);
    if(index==IG_VICTORY||index==IG_DEFEAT)return; /* the outcome is decided */
    if(index<0||ingame_dialogs[index].parent<0)M_ClearMenus();
    else show_ingame(ingame_dialogs[index].parent);
}
/* tbl strings joined by blank lines, colour bytes dropped. */
static void tbl_prose(const char *name,char *out,size_t size) {
    blob_t b={0};out[0]=0;
    if(!sc_read(asset_root,name,&b)||b.size<2){W_FreeFile(&b);return;}
    unsigned count=read_u16_le(b.bytes);size_t n=0;
    for(unsigned i=1;i<=count&&2u+i*2u<=b.size;i++) {
        unsigned off=read_u16_le(b.bytes+i*2);
        if(n&&n+2<size){out[n++]='\n';}
        for(unsigned p=off;p<b.size&&b.bytes[p]&&n+1<size;p++)
            if(b.bytes[p]>=32||b.bytes[p]=='\n')out[n++]=(char)b.bytes[p];
    }
    out[n]=0;W_FreeFile(&b);
}
static void restart_mission(void) {
    if(restart_path[0]){menumap=restart_path;M_ClearMenus();}
}
static void note_paths(void) {
    restart_path[0]=0;
    const char *path=level.map_path;
    if(!netgame&&path&&path[0]) {
        const char *install=strstr(path,"install/");
        snprintf(restart_path,sizeof(restart_path),"%s",install?install:path);
    }
}
static void ingame_action(menu_t *menu,menuitem_t *item,menuaction_t action) {
    if(action!=MA_ACTIVATE)return;
    int index=ingame_index(menu),id=item->id;
    switch(index) {
    case IG_GAME:
        if(id==65533)M_ClearMenus();
        else if(id==1)show_ingame(IG_SAVE);
        else if(id==2)show_ingame(IG_LOAD);
        else if(id==3)show_ingame(IG_OPTIONS);
        else if(id==4)show_ingame(IG_HELP_MENU);
        else if(id==5)show_ingame(IG_OBJECTIVES);
        else if(id==6)show_ingame(IG_END);
        return;
    case IG_OPTIONS:
        if(id>=1&&id<=3)show_ingame(id==1?IG_SPEED:id==2?IG_SOUND:IG_VIDEO);
        else if(id==65533)show_ingame(IG_GAME);
        return;
    case IG_SPEED: case IG_SOUND: case IG_VIDEO:
        if(id==65534||id==65533)show_ingame(IG_OPTIONS);
        return;
    case IG_SAVE: case IG_LOAD:
        if(id==65533)show_ingame(IG_GAME);
        else if(id==65534||id==3)M_StartMessage(index==IG_SAVE?"Saving games is not available yet.":
                                                "Loading saved games is not available yet.");
        return;
    case IG_OBJECTIVES:
        if(id==65533)show_ingame(IG_GAME);
        return;
    case IG_HELP_MENU:
        if(id==1||id==2) {
            tbl_prose(id==1?"rez/help_txt.tbl":"rez/tips.tbl",help_text,sizeof(help_text));
            menuitem_t *list=M_MenuFind(&ingame_screens[IG_HELP].menu,1);
            if(list){list->first_row=0;list->prose=help_text;}
            show_ingame(IG_HELP);
        } else if(id==65533)show_ingame(IG_GAME);
        return;
    case IG_HELP:
        if(id==65534)show_ingame(IG_HELP_MENU);
        return;
    case IG_END:
        if(id==1)show_ingame(IG_RESTART);
        else if(id==2)show_ingame(IG_QUIT_MISSION);
        else if(id==3)show_ingame(IG_QUIT);
        else if(id==65533)show_ingame(IG_GAME);
        return;
    case IG_RESTART:
        if(id==65534){note_paths();restart_mission();}
        else if(id==65533)show_ingame(IG_END);
        return;
    case IG_QUIT_MISSION:
        if(id==65534){menuleave=true;M_ClearMenus();}
        else if(id==65533)show_ingame(IG_END);
        return;
    case IG_QUIT:
        if(id==65534){if(menu->app)menu->app->running=false;M_ClearMenus();}
        else if(id==65533)show_ingame(IG_END);
        return;
    case IG_VICTORY:
        if(id==65534||id==65514)show_score();
        else if(id==1)M_ClearMenus(); /* Continue Playing */
        return;
    case IG_DEFEAT:
        if(id==65534)show_score();
        return;
    }
}
static void objectives_refresh(menu_t *menu) {
    const char *goal=sc_objectives_text();
    set_prose(M_MenuFind(menu,2),goal&&goal[0]?goal:objectives);
}
static void save_refresh(menu_t *menu) {
    menuitem_t *list=M_MenuFind(menu,1);
    if(list) {
        M_MenuSetRows(list,save_count);
        list->prose=save_count?NULL:no_saves;
    }
    fit_scrollbars(menu);
}
static bool load_ingame(app_t *app) {
    for(int i=0;i<IG_SCREENS;i++) {
        screen_t *screen=&ingame_screens[i];
        free_art(screen);
        if(!build_screen(screen,app,asset_root,ingame_dialogs[i].dialog,&ingame,NULL,ingame_action))return false;
        screen->menu.background=NULL;screen->menu.palette=NULL;
        screen->menu.escape=ingame_escape;
        for(int k=0;k<screen->menu.numitems;k++) {
            menuitem_t *item=&screen->items[k];
            if(item->kind==MI_SLIDER){item->range.min=0;item->range.max=100;item->value=50;}
        }
    }
    /* Network play and observers are multiplayer only; pause rows are hidden. */
    M_MenuFind(&ingame_screens[IG_OPTIONS].menu,4)->enabled=false;
    menuitem_t *observe=M_MenuFind(&ingame_screens[IG_QUIT_MISSION].menu,1);
    if(observe)observe->visible=false;
    observe=M_MenuFind(&ingame_screens[IG_DEFEAT].menu,1);
    if(observe)observe->visible=false;
    ingame_screens[IG_OBJECTIVES].menu.refresh=objectives_refresh;
    for(int i=IG_SAVE;i<=IG_LOAD;i++) {
        menuitem_t *list=M_MenuFind(&ingame_screens[i].menu,1);
        if(!list)return false;
        as_list(list,0,-1,save_row);list->prose=no_saves;
        ingame_screens[i].menu.refresh=save_refresh;
    }
    menuitem_t *field=M_MenuFind(&ingame_screens[IG_SAVE].menu,2);
    if(field){field->kind=MI_TEXTFIELD;field->maxchars=24;}
    menuitem_t *help=M_MenuFind(&ingame_screens[IG_HELP].menu,1);
    if(!help)return false;
    as_list(help,0,-1,NULL);help->prose=help_text;
    /* Video: animating portraits is the selected choice. */
    menuitem_t *animate=M_MenuFind(&ingame_screens[IG_VIDEO].menu,5);
    if(animate)animate->value=1;
    for(int k=3;k<=6;k++){menuitem_t *on=M_MenuFind(&ingame_screens[IG_SOUND].menu,k);if(on)on->value=1;}
    return true;
}
static void free_ingame(void) {
    for(int i=0;i<IG_SCREENS;i++)free_art(&ingame_screens[i]);
}

/* ── mission result and score ───────────────────────────────────────────── */

/* wmission or lmission over the world, then gluscore on the race's victory
 * or defeat theme (pal<race><v|d>, glue/score<race><v|d>/pmain.pcx). */
static screen_t score;
static int shown_result,score_tab;
static char score_theme[16];

void sc_show_result(int result) {
    shown_result=result;
    next_path[0]=0;
    if(!netgame&&result==1)sc_campaign_next(level.map_path,next_path,sizeof(next_path));
    note_paths();
    if(!ingame_screens[IG_VICTORY].menu.numitems){show_score();return;}
    screen_t *dialog=&ingame_screens[result==2?IG_DEFEAT:IG_VICTORY];
    if(result!=2) {
        /* A draw shows its own line and a plain Ok in place of Victory. */
        bool draw=result==3;
        M_MenuFind(&dialog->menu,65516)->visible=!draw;
        M_MenuFind(&dialog->menu,65513)->visible=draw;
        M_MenuFind(&dialog->menu,65534)->visible=!draw;
        menuitem_t *ok=M_MenuFind(&dialog->menu,65514);
        ok->visible=ok->enabled=draw;ok->kind=MI_BUTTON;
        if(draw)ok->rect=M_MenuFind(&dialog->menu,65534)->rect;
        M_MenuFind(&dialog->menu,1)->visible=!netgame&&!draw;
    }
    menuitem_t *first=M_MenuFind(&dialog->menu,result==3?65514:65534);
    dialog->menu.itemOn=first?(int)(first-dialog->items):-1;
    show_ingame(result==2?IG_DEFEAT:IG_VICTORY);
}

static int race_of(int owner) {
    int side=sc_player_side(owner);
    return side==0?1:side==2?2:0; /* SIDE: 0 Zerg, 1 Terran, 2 Protoss */
}
static const char *player_label(int owner) {
    if(owner==consoleplayer)return chat_name();
    return sc_owner_kind(owner)==5?"Computer":M_va("Player %d",owner+1);
}
typedef struct { int produced, killed, lost, built, razed, ruined, gas, minerals, spent; } sc_tally_t;
/* Produced counts what the player holds plus what it lost, starting units
 * included. Scores use units.dat build and destroy scores. */
static void tally(int owner,sc_tally_t *t,int *units,int *structures,int *resources) {
    memset(t,0,sizeof(*t));*units=*structures=0;
    int scores[2]={0,0};
    if(thinkercap.next)for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->hp<=0||mo->type_id<1||mo->type_id>SC_TYPES)continue;
        const sc_unit_t *u=&sc_units[mo->type_id-1];
        if(u->flags&1){t->built++;scores[1]+=u->build_score;}
        else if(u->build_score>0){t->produced++;scores[0]+=u->build_score;}
    }
    sc_stats_t stats;
    sc_player_stats(owner,&stats);
    for(int type=0;type<SC_TYPES;type++) {
        const sc_unit_t *u=&sc_units[type];
        bool building=(u->flags&1)!=0;
        if(!u->build_score&&!building)continue;
        if(building){t->ruined+=stats.lost[type];t->built+=stats.lost[type];t->razed+=stats.killed[type];
                     scores[1]+=stats.lost[type]*u->build_score+stats.killed[type]*u->destroy_score;}
        else{t->lost+=stats.lost[type];t->produced+=stats.lost[type];t->killed+=stats.killed[type];
             scores[0]+=stats.lost[type]*u->build_score+stats.killed[type]*u->destroy_score;}
    }
    t->minerals=stats.gathered[0];t->gas=stats.gathered[1];t->spent=stats.spent;
    *units=scores[0];*structures=scores[1];*resources=t->minerals+t->gas+t->spent;
}
static void paint_score(void) {
    if(!score.menu.numitems)return;
    static const char *const columns[4][3]={
        {"Units","Structures","Resources"},{"Produced","Killed","Lost"},
        {"Constructed","Razed","Lost"},{"Gas Mined","Minerals Mined","Total Spent"}};
    for(int c=0;c<3;c++)set_text(M_MenuFind(&score.menu,10+c),columns[score_tab][c]);
    for(int tab=0;tab<4;tab++)M_MenuFind(&score.menu,3+tab)->value=tab==score_tab;
    int row=0;
    for(int owner=0;owner<8;owner++) {
        int kind=sc_owner_kind(owner);
        if(kind!=5&&kind!=6)continue;
        if(row==8)break;
        int base=13+row*6;
        sc_tally_t t;int units,structures,resources;
        tally(owner,&t,&units,&structures,&resources);
        int values[4][3]={{units,structures,resources},{t.produced,t.killed,t.lost},
                          {t.built,t.razed,t.ruined},{t.gas,t.minerals,t.spent}};
        M_MenuFind(&score.menu,base)->visible=true;
        for(int k=1;k<=5;k++)M_MenuFind(&score.menu,base+k)->visible=true;
        set_text(M_MenuFind(&score.menu,base+1),player_label(owner));
        for(int c=0;c<3;c++)set_text(M_MenuFind(&score.menu,base+2+c),M_va("%d",values[score_tab][c]));
        set_text(M_MenuFind(&score.menu,base+5),M_va("%d",units+structures+resources));
        row++;
    }
    for(;row<8;row++)for(int k=0;k<6;k++)M_MenuFind(&score.menu,13+row*6+k)->visible=false;
    int seconds=sc_elapsed_ms()/1000;
    set_text(M_MenuFind(&score.menu,9),M_va("Elapsed Time: %d:%02d",seconds/60,seconds%60));
}
static void score_action(menu_t *menu,menuitem_t *item,menuaction_t action) {
    (void)menu;
    if(item->id>=3&&item->id<=6&&(action==MA_ACTIVATE||action==MA_CHANGE)){score_tab=item->id-3;paint_score();return;}
    if(action!=MA_ACTIVATE||item->id!=7)return;
    /* A campaign win briefs the next mission; a campaign loss briefs this one
     * again. Custom and network games end. */
    if(!netgame&&shown_result==1&&next_path[0]) {
        snprintf(campaign_map,sizeof(campaign_map),"%s",next_path);
        if(open_briefing(next_path))return;
        menumap=campaign_map;M_ClearMenus();return;
    }
    if(!netgame&&shown_result==2&&restart_path[0]&&strstr(restart_path,"campaign/")&&open_briefing(restart_path))return;
    menuleave=true;M_ClearMenus();
}
static void score_escape(menu_t *menu) { (void)menu; }
static void show_score(void) {
    static const char races[]="tzp";
    char theme[16],palette[64],picture[64];
    int race=race_of(consoleplayer);
    snprintf(theme,sizeof(theme),"%c%c",races[race],shown_result==1||shown_result==3?'v':'d');
    if(strcmp(theme,score_theme)||!score.menu.numitems) {
        free_art(&score);score_theme[0]=0;
        snprintf(palette,sizeof(palette),"glue/pal%s/backgnd.pcx",theme);
        if(!load_screen(&score,front.app,asset_root,"rez/gluscore.bin",palette,score_action)) {
            free_art(&score);M_ClearMenus();menuleave=true;return;
        }
        snprintf(score_theme,sizeof(score_theme),"%s",theme);
        snprintf(picture,sizeof(picture),"glue/score%s/pmain.pcx",theme);
        char path[2048];
        menuitem_t *back=M_MenuFind(&score.menu,1);
        artwork_t *art=(artwork_t *)back->userdata;
        native_path(path,sizeof(path),asset_root,picture);
        if(W_LoadIndexedSheet(path,&art->image)) {
            back->sheet=&art->image;
            for(int st=0;st<MS_STATES;st++)back->look[st].cell=0;
        }
        snprintf(picture,sizeof(picture),"glue/score%s/pinset.pcx",theme);
        native_path(path,sizeof(path),asset_root,picture);
        for(int row=0;row<8;row++) {
            menuitem_t *bar=M_MenuFind(&score.menu,13+row*6);
            artwork_t *a=(artwork_t *)bar->userdata;
            if(W_LoadIndexedSheet(path,&a->image)) {
                bar->sheet=&a->image;
                for(int st=0;st<MS_STATES;st++)bar->look[st].cell=0;
            }
        }
        for(int tab=0;tab<4;tab++){menuitem_t *t=M_MenuFind(&score.menu,3+tab);t->group=1;}
        score.menu.escape=score_escape;
        M_MenuFind(&score.menu,7)->hotkey=SDLK_RETURN;
    }
    set_text(M_MenuFind(&score.menu,2),shown_result==1?"Victory!":shown_result==3?"Draw!":"Defeat!");
    score_tab=0;
    score.menu.app=hud.app?hud.app:front.app;
    paint_score();
    menuitem_t *ok=M_MenuFind(&score.menu,7);
    score.menu.itemOn=(int)(ok-score.items);
    M_SetupNextMenu(&score.menu);
}

/* ── title ──────────────────────────────────────────────────────────────── */

/* titledlg over glue/title/title.pcx, in its own palette and font ramp,
 * until a key or click or three seconds. */
static screen_t title;
static theme_t title_theme;
static unsigned title_started;
static bool title_done;
static void leave_title(void) { title_done=true; M_SetupNextMenu(&front); }
static void title_action(menu_t *menu,menuitem_t *item,menuaction_t action) {
    (void)menu;(void)item;if(action==MA_ACTIVATE)leave_title();
}
static void title_escape(menu_t *menu) { (void)menu; leave_title(); }
static void title_tick(menu_t *menu) {
    M_MenuTicker(menu);
    if(SDL_GetTicks()-title_started>3000)leave_title();
}
static bool load_title(app_t *app) {
    char path[2048];
    native_path(path,sizeof(path),asset_root,"glue/title/title.pcx");
    if(!W_LoadIndexedSheet(path,&title_theme.background))return false;
    const char *names[]={"font10","font14","font16","font16x"};
    for(int i=0;i<4;i++)
        if(!sc_font(asset_root,names[i],&title_theme.fonts[i])||
           !sc_font_colors(asset_root,"glue/title/tfont.pcx",&title_theme.fonts[i]))return false;
    snprintf(title_theme.path,sizeof(title_theme.path),"glue/title");
    if(!build_screen(&title,app,asset_root,"rez/titledlg.bin",&title_theme,NULL,title_action))return false;
    if(title.menu.numitems==SC_DIALOG_CONTROLS)return false;
    /* The whole screen is one button: any click leaves. */
    title.items[title.menu.numitems++]=(menuitem_t){.kind=MI_BUTTON,.id=-5,.visible=true,.enabled=true,
        .rect={0,0,640,480},.routine=title_action,.hotkey=SDLK_RETURN};
    title.menu.escape=title_escape;
    title.menu.ticker=title_tick;
    title.menu.refresh=NULL;
    return true;
}

bool G_InitMenus(app_t *app,const char *root) {
    snprintf(asset_root,sizeof(asset_root),"%s",root);
    const char *names[]={"font10","font14","font16","font16x"};
    for(int i=0;i<4;i++) {
        if(!sc_font(root,names[i],&gamefonts[i]))return false;
        if(!sc_font_colors(root,"game/tfontgam.pcx",&gamefonts[i]))return false;
    }
    if(!load_screen(&main_screen,app,root,"rez/glumain.bin","glue/palmm/backgnd.pcx",NULL))return false;
    front=main_screen.menu;
    for(int i=0;i<front.numitems;i++) {
        menuitem_t *item=&front.items[i];
        item->routine=item->id==3?begin:item->id==2?M_MenuQuitGame:item->id==4?open_multi:unavailable;
    }
    if(!load_screen(&registry,app,root,"rez/glulogin.bin","glue/palnl/backgnd.pcx",frontend)||
       !load_screen(&new_id,app,root,"rez/glunewch.bin","glue/palnl/backgnd.pcx",frontend)||
       !load_screen(&campaign,app,root,"rez/glucmpgn.bin","glue/palcs/backgnd.pcx",frontend)||
       !load_screen(&briefings[0],app,root,"rez/glurdyt.bin","glue/palrt/backgnd.pcx",frontend)||
       !load_screen(&briefings[1],app,root,"rez/glurdyz.bin","glue/palrz/backgnd.pcx",frontend)||
       !load_screen(&briefings[2],app,root,"rez/glurdyp.bin","glue/palrp/backgnd.pcx",frontend)||
       !load_screen(&load_game,app,root,"rez/gluload.bin","glue/palnl/backgnd.pcx",load_action)||
       !load_screen(&custom,app,root,"rez/glucustm.bin","glue/palnl/backgnd.pcx",custom_action)||
       !load_screen(&conn,app,root,"rez/gluconn.bin","glue/palnl/backgnd.pcx",multi)||
       !load_screen(&join,app,root,"rez/glujoin.bin","glue/palnl/backgnd.pcx",multi)||
       !load_screen(&create,app,root,"rez/glucreat.bin","glue/palnl/backgnd.pcx",multi)||
       !load_screen(&chat,app,root,"rez/gluchat.bin","glue/palnl/backgnd.pcx",multi))return false;
    if(!wire_multi()||!wire_single())return false;
    screen_t *singles[]={&registry,&new_id,&campaign,&briefings[0],&briefings[1],&briefings[2],&load_game,&custom};
    for(unsigned s=0;s<sizeof(singles)/sizeof(*singles);s++)singles[s]->menu.escape=single_escape;
    menuitem_t *list=M_MenuFind(&registry.menu,8);
    as_list(list,1,0,registry_row);
    M_MenuFind(&registry.menu,7)->enabled=false;
    menuitem_t *field=M_MenuFind(&new_id.menu,3);
    field->kind=MI_TEXTFIELD;field->maxchars=31;field->enabled=field->visible=true;
    snprintf(field->text,sizeof(field->text),"%s",player_name);
    new_id.menu.itemOn=(int)(field-new_id.items);
    for(int r=0;r<3;r++) {
        menu_t *menu=&briefings[r].menu;
        menuitem_t *text=M_MenuFind(menu,65526);
        if(!text||!M_MenuFind(menu,65525)||!M_MenuFind(menu,65524))return false;
        text->kind=MI_STATIC;text->visible=true;text->prose=briefing_text;
        text=M_MenuFind(menu,65525);text->visible=true;text->prose=objectives;
        M_MenuFind(menu,65524)->visible=false;
        menu->ticker=briefing_tick;
        for(int slot=0;slot<4;slot++) {
            menuitem_t *frame=M_MenuFind(menu,15+slot);
            if(!frame)return false;
            frame->visible=true;frame->ownerdraw=draw_slot;frame->text[0]=0;
        }
    }
    for(int i=0;i<4;i++)ingame.fonts[i]=gamefonts[i];
    if(!load_title(app))return false;
    return true;
}
menu_t *G_ControlPanel(app_t *app,bool inlevel) {
    menu_t *menu=inlevel?&ingame_screens[IG_GAME].menu:&front;
    /* The title shows once, before the first main menu. */
    if(!inlevel&&!title_done&&title.menu.numitems){title_started=SDL_GetTicks();menu=&title.menu;}
    if(inlevel&&!menu->numitems)menu=&front;
    menu->app=app;
    if(inlevel)ingame_screens[IG_GAME].menu.itemOn=-1;
    return menu;
}
void G_ShutdownMenus(void) {
    M_NetStop();
    screen_t *screens[]={&main_screen,&registry,&new_id,&campaign,&briefings[0],&briefings[1],&briefings[2],
        &load_game,&custom,&notice,&confirm,&conn,&join,&create,&chat,&score,&title};
    for(unsigned s=0;s<sizeof(screens)/sizeof(*screens);s++)free_art(screens[s]);
    free_ingame();score_theme[0]=0;title_done=false;
    for(int i=0;i<4;i++)hide_slot(i);
    R_FreeSprite(&title_theme.background);
    for(int i=0;i<4;i++)R_FreeSprite(&title_theme.fonts[i].sprite);
    memset(&title_theme,0,sizeof(title_theme));
    beneath=NULL;
    while(themes) {
        theme_t *theme=themes;themes=theme->next;
        R_FreeSprite(&theme->background);R_FreeSprite(&theme->widgets);R_FreeSprite(&theme->panel);
        for(int i=0;i<4;i++)R_FreeSprite(&theme->fonts[i].sprite);
        free(theme);
    }
    for(int i=0;i<4;i++)R_FreeSprite(&gamefonts[i].sprite);
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
/* An aimed ability waits for a click on the world; the rest go at once.
 * TC_SPELL carries the techdata row; the game picks who casts. */
static void ability(menu_t *m,menuitem_t *i,menuaction_t a) {
    if(a==MA_ACTIVATE&&sc_tech_aimed(i->value)){M_MenuTarget(m,i);return;}
    if(a!=MA_ACTIVATE&&a!=MA_TARGET)return;
    ticcmd_t order={.order=TC_SPELL,.product=i->value};
    if(a==MA_TARGET) {
        ivec2_t at=R_ScreenToMapGrid(m->app,&level,m->cursor.x,m->cursor.y);
        int picked=R_PickUnit(m->app,&level,hudview.units,hudview.unit_count,NULL,
            hudview.sprites,gameinfo,m->cursor.x,m->cursor.y,-1);
        order.position=fixed3_from_fvec2(fvec2_cell_center(at),0);
        order.target=picked>=0?hudview.units[picked]->id:0;
    }
    for(int j=0;j<hudview.unit_count&&order.count<MAXCOMMANDUNITS;j++) {
        mobj_t *u=hudview.units[j];
        if(P_MobjIsSelected(u)&&u->owner==consoleplayer&&u->hp>0)order.units[order.count++]=u->id;
    }
    if(order.count)G_QueueTiccmd(&order);
}
static void unload(menu_t *m,menuitem_t *i,menuaction_t a) {
    (void)m;(void)i;if(a==MA_ACTIVATE)HU_SelectedOrder(TC_UNLOAD,(fvec2_t){0},0);
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
            /* Morphs and larvae take the order where they stand. */
            if(p&&p->worker_build)HU_BuildProduct(m,i,u,p);
            else if(p)G_BuildOrder(u,p->ui_id);
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
    int used=0,have=0;
    sc_supply_counts(consoleplayer,&used,&have);
    snprintf(huditems[resource_start+2].text,128,"Supply %d/%d",used/2,have/2);
    const char *goal=sc_objectives_text();
    if(goal&&goal[0]) { huditems[tip_item].prose=goal; huditems[tip_item].visible=true; }
    else { huditems[tip_item].prose=start_tip; huditems[tip_item].visible=start_tip[0]!=0; }
    for(int i=selection_start;i<command_start;i++)huditems[i].visible=false;
    for(int i=0;i<9;i++)huditems[command_start+i].visible=false;
    huditems[portrait_item].visible=false;
    for(int i=0;i<SC_SELECTION_SLOTS;i++)huditems[selection_icon_start+i].visible=false;
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
    StaticProductDefinition list[SC_TYPES];int n=G_ModelGetProducts(NULL,consoleplayer,list,SC_TYPES);
    /* A unit's own morph (Guardian) goes under its move and attack row. */
    int slot=!build_page&&(selected->traits&MF_MOBILE)?6:0;
    static const SDL_Keycode basic[]={SDLK_c,SDLK_s,SDLK_r,SDLK_b,SDLK_a,SDLK_e,SDLK_t,SDLK_u};
    for(int i=0;i<n;i++) {
        const StaticProductDefinition *p=&list[i];
        bool made=false;
        for(int j=0;j<p->maker_count;j++)made|=p->makers[j]==selected->type_id;
        if(!made)continue;
        if(p->worker_build) {
            if(!build_page)continue;
            int index=slot++;
            if((build_page==1&&index>=8)||(build_page==2&&index<8))continue;
            int pos=build_page==1?index:index-8;
            button(pos,p->icon_frame,selected->type_id==MT_SCV&&build_page==1?basic[pos]:SDLK_1+pos,product,p->ui_id,p->label);
            huditems[command_start+pos].enabled=G_ModelProductAvailable(NULL,consoleplayer,p);
        } else if(!build_page) {
            if(!sc_upgrade_offered(consoleplayer,p))continue;
            /* A hatchery shows what its larvae can become now, beside its own morph. */
            if(p->makers[0]!=selected->type_id&&!G_ModelProductAvailable(NULL,consoleplayer,p))continue;
            if(slot>=9)continue;
            button(slot,p->icon_frame,slot==0?SDLK_t:SDLK_1+slot,product,p->ui_id,p->label);
            huditems[command_start+slot++].enabled=G_ModelProductAvailable(NULL,consoleplayer,p);
        }
    }
    /* Abilities follow, greyed until researched; a Bunker unloads. */
    if(!build_page) {
        int techs[8],n_techs=sc_unit_techs(selected->type_id,techs,8);
        for(int t=0;t<n_techs&&slot<9;t++) {
            int tech=techs[t];
            int icon=tech==SC_TECH_NUCLEAR_STRIKE?311:tech==SC_TECH_SIEGE_MODE&&selected->type_id==MT_SIEGE_MODE?246:
                sc_techs[tech].icon;
            button(slot,icon,SDLK_1+slot,ability,tech,tech==SC_TECH_NUCLEAR_STRIKE?"Nuclear Strike":sc_techs[tech].name);
            huditems[command_start+slot++].enabled=sc_has_tech(consoleplayer,tech);
        }
        if(sc_units[selected_id].space_provided&&(sc_units[selected_id].flags&SC_UNIT_BUILDING)&&slot<9)button(slot++,312,SDLK_u,unload,0,"Unload All");
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
    static const char *const race_art[]={"dlgs/terran.grp","dlgs/zerg.grp","dlgs/protoss.grp"};
    R_FreeSprite(&ingame.widgets);R_FreeSprite(&ingame.panel);
    if(!grp(root,race_art[race_of(consoleplayer)],console.palette,&ingame.widgets)||
       !grp(root,"dlgs/tile.grp",console.palette,&ingame.panel))return NULL;
    if(!grp(root,"unit/wirefram/wirefram.grp",console.palette,&wireframe)||
       !grp(root,"unit/cmdbtns/cmdicons.grp",console.palette,&icons))return NULL;
    if(!palette_from(root,"game/tunit.pcx",&console,0)||
       !palette_from(root,"game/tunit.pcx",&ingame.widgets,0)||
       !palette_from(root,"game/tunit.pcx",&ingame.panel,0)||
       !palette_from(root,"game/twire.pcx",&wireframe,0)||
       !palette_from(root,"unit/cmdbtns/ticon.pcx",&icons,16))return NULL;
    hud.app=app;hud.refresh=refresh;hud.numitems=1;
    huditems[0]=(menuitem_t){.kind=MI_STATIC,.visible=true,.rect={0,0,640,480},.passthrough=true,.sheet=&console,.stretch=true};
    int first=append_dialog(root,"rez/minimap.bin");if(first<0)return NULL;
    for(int i=first;i<hud.numitems;i++)huditems[i].visible=false;
    huditems[first].visible=huditems[first].enabled=true;huditems[first].kind=MI_MINIMAP;huditems[first].ownerdraw=minimap;
    first=append_dialog(root,"rez/stat_f10.bin");if(first<0)return NULL;
    huditems[first].routine=open_menu;huditems[first].hotkey=SDLK_F10;huditems[first].sheet=&ingame.widgets;
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
    huditems[hud.numitems++]=(menuitem_t){.kind=MI_STATIC,.visible=true,
        .rect={430,22,200,16},.font=&gamefonts[0],.align=MALIGN_RIGHT};
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
    for(int i=0;i<4;i++)ingame.fonts[i]=gamefonts[i];
    if(!load_ingame(app))return NULL;
    tip_item=hud.numitems++;
    huditems[tip_item]=(menuitem_t){.kind=MI_STATIC,.visible=false,.passthrough=true,.rect={58,190,300,112},
        .font=&gamefonts[0],.prose=start_tip,.opaque=false};
    return &hud;
}
void G_ShutdownHUD(void) {
    R_FreeSprite(&console);R_FreeSprite(&wireframe);R_FreeSprite(&icons);R_FreeSprite(&ingame.widgets);R_FreeSprite(&ingame.panel);free_ingame();R_FreeSprite(&portrait.sheet);free(portrait.palettes);memset(&portrait,0,sizeof(portrait));portrait_id=-1;hudview=(hudview_t){0};
}
