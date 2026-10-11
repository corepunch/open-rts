#include "sc_local.h"
#include <stdio.h>

/* Stargus gameui.lua and ui.lua locate the retail five-slot pipeline at
 * (240,396), (240,434), (278,434), (316,434), (354,434). The artwork is
 * native tcmdbtns/pcmdbtns/zcmdbtns GRP, not generated button art. */
enum { SC_QUEUE_SLOTS=5, SC_QUEUE_FRAME=2, SC_QUEUE_FIRST_NUMBER=6 };
static const irect_t queue_rect[SC_QUEUE_SLOTS]={
    {240,396,36,36}, {240,434,36,36}, {278,434,36,36},
    {316,434,36,36}, {354,434,36,36}
};
static spritesheet_t frame, bar_empty, bar_full;
static const spritesheet_t *glyphs;
static const mobj_t *shown;
static menuitem_t *status_items;
static int slot_start, caption_item, progress_item;

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

bool sc_status_init(menu_t *menu,menuitem_t *items,int capacity,const char *root,
                    const uint32_t *palette,bitmapfont_t *font,const spritesheet_t *cmdicons) {
    if(menu->numitems+SC_QUEUE_SLOTS+2>capacity)return false;
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
    status_items=items;
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
    const production_t *queue=shown?shown->production:NULL;
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
    glyphs=NULL;shown=NULL;status_items=NULL;
}
