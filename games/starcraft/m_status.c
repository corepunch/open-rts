#include "sc_local.h"
#include <stdio.h>

/* Stargus gameui.lua and ui.lua locate the retail five-slot pipeline at
 * (240,396), (240,434), (278,434), (316,434), (354,434). The artwork is
 * native tcmdbtns/pcmdbtns/zcmdbtns GRP, not generated button art. */
enum { SC_QUEUE_SLOTS=5, SC_QUEUE_FRAME=2, SC_QUEUE_FIRST_NUMBER=6 };
enum { SC_EQUIPMENT_FRAME=12 };
static const irect_t queue_rect[SC_QUEUE_SLOTS]={
    {240,396,36,36}, {240,434,36,36}, {278,434,36,36},
    {316,434,36,36}, {354,434,36,36}
};
static spritesheet_t frame, bar_empty, bar_full;
static const spritesheet_t *glyphs, *wireframes;
static const mobj_t *shown;
static menuitem_t *status_items;
static int slot_start, caption_item, progress_item;
static int wire_item, name_item, hp_item, second_item, rank_item, kills_item;
static int equipment_item[2], equipment_level[2];

static int status_control(int first,int end,int id) {
    for(int i=first;i<end;i++)if(status_items[i].id==id)return i;
    return -1;
}

static void draw_equipment(const menu_t *menu,const menuitem_t *item,irect_t rect) {
    (void)menu;
    int icon=item->value;
    if(icon<0||icon>=glyphs->numlumps)return;
    irect_t src=glyphs->cells[icon].rect;
    irect_t dst={rect.x+(rect.w-src.w*rect.w/36)/2,
                 rect.y+(rect.h-src.h*rect.h/36)/2,
                 src.w*rect.w/36,src.h*rect.h/36};
    R_DrawSprite(glyphs,icon,-1,&src,&dst,0,16);
    R_DrawSprite(&frame,SC_EQUIPMENT_FRAME,-1,NULL,&rect,0,16);
}

static void draw_queue(const menu_t *menu,const menuitem_t *item,irect_t rect) {
    (void)menu;
    if(!shown||!shown->production)return;
    int slot=item->value;
    const production_t *queue=shown->production;
    irect_t border=frame.cells[SC_QUEUE_FRAME].rect;
    R_DrawSprite(&frame,SC_QUEUE_FRAME,-1,&border,&rect,0,16);
    productionitem_t queued=P_ProductionItem(queue,slot<queue->queue_count?slot:0);
    const StaticProductDefinition *p=G_ModelProductByClassType(NULL,queued.product_class,queued.product_type);
    int art=slot<queue->queue_count?
        (p?p->icon_frame:-1):
        SC_QUEUE_FIRST_NUMBER+slot;
    const spritesheet_t *sheet=slot<queue->queue_count?glyphs:&frame;
    if(art<0||art>=sheet->numlumps)return;
    const spritecell_t *cell=&sheet->cells[art];
    irect_t src=cell->rect;
    irect_t dst={rect.x+(rect.w-src.w*rect.w/36)/2,
                 rect.y+(rect.h-src.h*rect.h/36)/2,
                 src.w*rect.w/36,src.h*rect.h/36};
    R_DrawSprite(sheet,art,-1,&src,&dst,0,16);
}

static void draw_progress(const menu_t *menu,const menuitem_t *item,irect_t rect) {
    (void)menu;(void)item;
    if(!shown||!shown->production)return;
    const production_t *queue=shown->production;
    R_DrawSprite(&bar_empty,0,-1,NULL,&rect,0,16);
    if(queue->time_ms<=0)return;
    int done=queue->time_ms-queue->time_left_ms;
    if(done<0)done=0;
    if(done>queue->time_ms)done=queue->time_ms;
    irect_t src={0,0,done*bar_full.frame_size.w/queue->time_ms,bar_full.frame_size.h};
    if(src.w>0) {
        irect_t dst={rect.x,rect.y,src.w*rect.w/bar_full.frame_size.w,rect.h};
        R_DrawSprite(&bar_full,0,-1,&src,&dst,0,16);
    }
}

static void cancel_slot(menu_t *menu,menuitem_t *item,menuaction_t action) {
    (void)menu;
    if(action!=MA_ACTIVATE||!shown||!shown->production||
       item->value>=shown->production->queue_count||shown->owner!=consoleplayer)return;
    ticcmd_t cmd={.order=TC_CANCEL_PRODUCTION,.product=item->value,
                  .count=1,.units={shown->id}};
    G_QueueTiccmd(&cmd);
}

bool sc_status_init(menu_t *menu,menuitem_t *items,int capacity,int stat_first,int stat_end,
                    const char *root,const uint32_t *palette,bitmapfont_t *font,
                    spritesheet_t *wireframe,const spritesheet_t *cmdicons) {
    if(menu->numitems+SC_QUEUE_SLOTS+4>capacity)return false;
    char path[2048];blob_t data={0};
    static const char *const race_frames[]={
        "unit/cmdbtns/tcmdbtns.grp","unit/cmdbtns/zcmdbtns.grp","unit/cmdbtns/pcmdbtns.grp"};
    int side=sc_player_side(consoleplayer);
    int race=side==0?1:side==2?2:0;
    sc_asset_path(path,sizeof(path),root,race_frames[race]);
    if(!W_ReadFile(path,&data))return false;
    bool ok=sc_decode_grp(&data,palette,false,&frame);
    W_FreeFile(&data);
    if(!ok)return false;
    sc_asset_path(path,sizeof(path),root,"game/tpbrempt.pcx");
    if(!W_LoadIndexedSheet(path,&bar_empty)){sc_status_shutdown();return false;}
    sc_asset_path(path,sizeof(path),root,"game/tpbrfull.pcx");
    if(!W_LoadIndexedSheet(path,&bar_full)){sc_status_shutdown();return false;}
    glyphs=cmdicons;
    wireframes=wireframe;
    status_items=items;
    wire_item=status_control(stat_first,stat_end,1);
    name_item=status_control(stat_first,stat_end,65531);
    hp_item=status_control(stat_first,stat_end,65529);
    second_item=status_control(stat_first,stat_end,65528);
    rank_item=status_control(stat_first,stat_end,65516);
    kills_item=status_control(stat_first,stat_end,65515);
    equipment_item[0]=status_control(stat_first,stat_end,9);
    equipment_item[1]=status_control(stat_first,stat_end,10);
    if(wire_item<0||name_item<0||hp_item<0||second_item<0||rank_item<0||kills_item<0||
       equipment_item[0]<0||equipment_item[1]<0){sc_status_shutdown();return false;}
    items[wire_item].sheet=wireframe;
    items[wire_item].font=NULL;
    int labels[]={name_item,hp_item,second_item,rank_item,kills_item};
    for(unsigned i=0;i<sizeof(labels)/sizeof(*labels);i++) {
        menuitem_t *item=&items[labels[i]];
        item->font=font;item->align=MALIGN_HCENTER;item->passthrough=true;
        item->visible=false;
    }
    for(int i=0;i<2;i++) {
        menuitem_t *item=&items[equipment_item[i]];
        item->kind=MI_STATIC;item->visible=false;item->passthrough=true;
        item->sheet=NULL;item->font=NULL;item->ownerdraw=draw_equipment;
        equipment_level[i]=menu->numitems;
        items[menu->numitems++]=(menuitem_t){.kind=MI_STATIC,.visible=false,.passthrough=true,
            .rect={item->rect.x+20,item->rect.y+22,14,12},.font=font,.align=MALIGN_RIGHT};
    }
    slot_start=menu->numitems;
    for(int slot=0;slot<SC_QUEUE_SLOTS;slot++) {
        menuitem_t *item=&items[menu->numitems++];
        *item=(menuitem_t){.kind=MI_BUTTON,.rect=queue_rect[slot],
            .visible=false,.enabled=false,.value=slot,.routine=cancel_slot,.ownerdraw=draw_queue,
            .release=true,.tooltip="Cancel queued unit"};
    }
    caption_item=menu->numitems;
    items[menu->numitems++]=(menuitem_t){.kind=MI_STATIC,.rect={316,410,85,14},
        .visible=false,.passthrough=true,.font=font};
    progress_item=menu->numitems;
    items[menu->numitems++]=(menuitem_t){.kind=MI_STATIC,.rect={278,422,108,9},
        .visible=false,.passthrough=true,.ownerdraw=draw_progress};
    return true;
}

void sc_status_refresh(const mobj_t *selected,int selection_count) {
    shown=selection_count==1?selected:NULL;
    const sc_unit_t *unit=sc_unit(shown);
    status_items[wire_item].visible=unit&&shown->type_id-1<wireframes->numlumps;
    if(unit)for(int state=0;state<MS_STATES;state++)
        status_items[wire_item].look[state].cell=shown->type_id-1;
    status_items[name_item].visible=status_items[hp_item].visible=unit!=NULL;
    status_items[second_item].visible=false;
    status_items[rank_item].visible=unit&&unit->rank[0];
    status_items[kills_item].visible=unit&&!(unit->flags&SC_UNIT_BUILDING);
    if(unit) {
        snprintf(status_items[name_item].text,sizeof(status_items[name_item].text),"%s",unit->name);
        snprintf(status_items[hp_item].text,sizeof(status_items[hp_item].text),"%d/%d",shown->hp,shown->max_hp);
        snprintf(status_items[rank_item].text,sizeof(status_items[rank_item].text),"%s",unit->rank);
        snprintf(status_items[kills_item].text,sizeof(status_items[kills_item].text),"Kills: %u",shown->sc.kills);
        menuitem_t *second=&status_items[second_item];
        if(unit->flags&SC_UNIT_SPELLCASTER) {
            second->visible=true;
            snprintf(second->text,sizeof(second->text),"%d/%d",sc_energy(shown),sc_max_energy(shown)>>8);
        } else if(unit->shields) {
            second->visible=true;
            snprintf(second->text,sizeof(second->text),"%d/%d",sc_shields(shown),unit->shields);
        }
    }
    bool equipment=unit&&!(unit->flags&SC_UNIT_BUILDING)&&shown->owner==consoleplayer;
    const sc_weapon_t *weapon=unit?sc_weapon(unit->ground_weapon):NULL;
    if(!weapon&&unit)weapon=sc_weapon(unit->air_weapon);
    for(int i=0;i<2;i++) {
        bool visible=equipment&&(i==0?unit->armor_upgrade>=0&&unit->armor_upgrade<SC_UPGRADES:
                                 weapon&&weapon->icon>=0&&weapon->icon<glyphs->numlumps);
        status_items[equipment_item[i]].visible=status_items[equipment_level[i]].visible=visible;
        if(visible)status_items[equipment_item[i]].value=
            i?weapon->icon:sc_upgrades[unit->armor_upgrade].icon;
        if(visible)snprintf(status_items[equipment_level[i]].text,
            sizeof(status_items[equipment_level[i]].text),"%d",
            sc_upgrade_level(shown->owner,i?weapon->upgrade:unit->armor_upgrade));
    }
    const production_t *queue=unit&&(unit->flags&SC_UNIT_BUILDING)?shown->production:NULL;
    if(queue&&queue->queue_count<=0)queue=NULL;
    for(int slot=0;slot<SC_QUEUE_SLOTS;slot++) {
        menuitem_t *item=&status_items[slot_start+slot];
        item->visible=queue&&!queue->placed;
        item->enabled=queue&&slot<queue->queue_count&&!queue->placed&&shown->owner==consoleplayer;
    }
    menuitem_t *caption=&status_items[caption_item];
    caption->visible=queue!=NULL;
    if(queue)snprintf(caption->text,sizeof(caption->text),"%s",
        queue->product_class==RTS_PRODUCT_UPGRADE?"Researching":
        queue->placed?"Constructing":"Building");
    status_items[progress_item].visible=queue!=NULL;
}

void sc_status_shutdown(void) {
    R_FreeSprite(&frame);R_FreeSprite(&bar_empty);R_FreeSprite(&bar_full);
    glyphs=wireframes=NULL;shown=NULL;status_items=NULL;
}
