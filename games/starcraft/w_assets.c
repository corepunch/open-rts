#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
uint32_t sc_palette[256];
void sc_asset_path(char *path,size_t size,const char *root,const char *name) {
    snprintf(path,size,"%s/native/%s",root,name);
    for(char *p=path+strlen(root)+1;*p;p++) { if(*p=='\\') *p='/'; *p=(char)tolower((unsigned char)*p); }
    if(access(path,R_OK)==0)return;
    snprintf(path,size,"%s/install/%s",root,name);
    for(char *p=path+strlen(root)+1;*p;p++) { if(*p=='\\') *p='/'; *p=(char)tolower((unsigned char)*p); }
}
bool sc_read(const char *root,const char *name,blob_t *out) {
    char path[2048];sc_asset_path(path,sizeof(path),root,name);
    return W_ReadFile(path,out);
}
bool sc_decode_grp(const blob_t *file,const uint32_t *palette,bool turns,spritesheet_t *out) {
    memset(out,0,sizeof(*out));
    if(!file||file->size<6) return false;
    const uint8_t *b=file->bytes;
    int count=read_u16_le(b),cw=read_u16_le(b+2),ch=read_u16_le(b+4);
    if(count<1||count>4096||cw<1||cw>1024||ch<1||ch>1024||file->size<6u+count*8u) return false;
    size_t raw_size=read_u32_le(b+10);
    for(int f=0;f<count;f++) {
        const uint8_t *r=b+6+f*8; bool unique=true;
        for(int j=0;j<f;j++) if(read_u32_le(b+10+j*8)==read_u32_le(r+4)) { unique=false; break; }
        if(unique) raw_size+=(size_t)r[2]*r[3];
    }
    bool raw=raw_size==file->size;
    if(!R_AllocSpriteCells(out,count)) return false;
    out->indexed=true; out->frame_size=(isize2_t){cw,ch};
    memcpy(out->palette,palette,sizeof(out->palette));
    memcpy(out->source_palette,palette,sizeof(out->palette));
    out->palette[0]=out->source_palette[0]=0;
    for(int f=0;f<count;f++) {
        const uint8_t *r=b+6+f*8;
        int xoff=r[0],yoff=r[1],w=r[2],h=r[3];
        size_t base=read_u32_le(r+4);
        if(xoff+w>cw||yoff+h>ch||base>file->size||(!raw && h*2u>file->size-base)) goto bad;
        /* Empty native pictures still have a cell, but no visible bounds. */
        int aw=w?w:1,ah=h?h:1;
        uint8_t *pixels=calloc((size_t)aw*ah,1); if(!pixels) goto bad;
        out->lumps[f].indices=pixels;
        /* The crop offset lives in ground_point only; a displacement too
         * would shift unflipped layers right of their selection bounds. */
        out->cells[f]=(spritecell_t){.rect={0,0,aw,ah},.bounds={0,0,w,h},
            .ground_point={cw/2-xoff,ch/2-yoff}};
        if(raw) {
            if((size_t)w*h>file->size-base) goto bad;
            memcpy(pixels,b+base,(size_t)w*h); continue;
        }
        for(int y=0;y<h;y++) {
            size_t at=base+read_u16_le(b+base+y*2); int x=0;
            while(x<w) {
                if(at>=file->size) goto bad;
                unsigned code=b[at++],n=code&0x80?code&0x7f:code&0x40?code&0x3f:code;
                if(!n||n>(unsigned)(w-x)) goto bad;
                if(code&0x80) { x+=(int)n; continue; }
                if(code&0x40) {
                    if(at>=file->size) goto bad;
                    memset(pixels+y*aw+x,b[at++],n);
                } else {
                    if(n>file->size-at) goto bad;
                    memcpy(pixels+y*aw+x,b+at,n); at+=n;
                }
                x+=(int)n;
            }
        }
    }
    /* Native GRPs store north..south clockwise (17 images); renderer slots
     * run counterclockwise. This is sprite facing, never a world-axis flip. */
    /* A turning sheet's sets of 17 are followed by one single-direction
     * frame per picture, for script frames that do not turn (deaths). */
    int frames=turns?(count+16)/17:count;
    if(!R_InitSpriteDef(out,turns?frames+count:frames,turns?32:1)) goto bad;
    for(int f=0;f<frames;f++) for(int d=0;d<(turns?32:1);d++) {
        int facing=(32-d)%32,slot=facing<=16?facing:32-facing;
        int lump=turns?f*17+slot:f;
        if(lump>=count) continue;
        if(!R_InstallSpriteLump(out,f,d,lump,turns&&facing>16)) goto bad;
    }
    if(turns) for(int lump=0;lump<count;lump++) {
        spriteframe_t *exact=&out->spritedef.spriteframes[frames+lump];
        free(exact->directions); exact->directions=NULL; exact->rotations=0;
        if(!R_AllocSpriteDirections(exact,1)||!R_InstallSpriteLump(out,frames+lump,0,lump,false)) goto bad;
    }
    return true;
bad:
    R_FreeSprite(out); return false;
}

bool sc_load_tiles(const char *root,const level_t *map,tileset_t *out) {
    blob_t vx={0},vr={0},cv={0},pal={0},vf={0}; bool ok=false;
    const char *extensions[]={"vx4","vr4","cv5","wpe","vf4"};
    blob_t *files[]={&vx,&vr,&cv,&pal,&vf};
    memset(out,0,sizeof(*out));
    for(int i=0;i<5;i++) {
        char path[128]; snprintf(path,sizeof(path),"tileset/%s.%s",map->tileset_name,extensions[i]);
        if(!sc_read(root,path,files[i])) goto done;
    }
    if(!vx.size||vx.size%32||!vr.size||vr.size%64||!cv.size||cv.size%52||pal.size!=1024||vf.size!=vx.size) goto done;
    out->count=(int)(vx.size/32); out->tile_w=out->tile_h=32; out->atlas_cols=16;
    out->indices=malloc((size_t)out->count*1024);
    out->tile_lookup_count=(int)(cv.size/52)*16;
    out->tile_lookup=malloc((size_t)out->tile_lookup_count*sizeof(int));
    if(!out->indices||!out->tile_lookup) goto done;
    for(int i=0;i<256;i++) {
        const uint8_t *p=pal.bytes+i*4;
        out->palette[i]=0xff000000u|p[0]<<16|p[1]<<8|p[2];
    }
    memcpy(sc_palette,out->palette,sizeof(sc_palette));
    for(int i=0;i<out->tile_lookup_count;i++) {
        unsigned tile=read_u16_le(cv.bytes+(i/16)*52+20+(i%16)*2);
        if(tile>=(unsigned)out->count) goto done;
        out->tile_lookup[i]=(int)tile;
    }
    for(int i=0;i<out->count;i++) for(int m=0;m<16;m++) {
        unsigned ref=read_u16_le(vx.bytes+i*32+m*2),index=ref>>1;
        if(index>=vr.size/64) goto done;
        for(int y=0;y<8;y++) for(int x=0;x<8;x++)
            out->indices[i*1024+(m/4*8+y)*32+m%4*8+x]=vr.bytes[index*64+y*8+((ref&1)?7-x:x)];
    }
    for(size_t i=0;i<(size_t)map->width*map->height;i++) {
        unsigned id=map->tile_ids[i];
        if(id>=(unsigned)out->tile_lookup_count) goto done;
        /* The engine currently paths in 32px cells. Keep mixed cells open;
         * wholly unwalkable VF4 megatiles (water, void) block ground units. */
        unsigned walkable=0,tile=(unsigned)out->tile_lookup[id];
        for(int m=0;m<16;m++) walkable|=read_u16_le(vf.bytes+tile*32+m*2)&1;
        if(!walkable) map->blocked[i]=1;
    }
    ok=true;
done:
    for(int i=0;i<5;i++) W_FreeFile(files[i]);
    if(!ok) R_FreeTileset(out);
    return ok;
}

const char *sc_tbl_string(const blob_t *tbl,unsigned index) {
    if(tbl->size<2||!index||index>read_u16_le(tbl->bytes)||2+index*2>tbl->size) return NULL;
    unsigned off=read_u16_le(tbl->bytes+index*2);
    return off<tbl->size&&memchr(tbl->bytes+off,0,tbl->size-off)?(char*)tbl->bytes+off:NULL;
}

/* Compile the visual portion of an IScript path into the shared state table.
 * Opcode lengths and header entry counts are PyMS IScriptBIN.py's. Sound,
 * movement and most combat opcodes are skipped; the first attack opcode puts
 * A_Attack on the frame it precedes. Later ones in the same pass are the
 * weapon's further hits (two attackmelee for the Zealot), which the engine
 * deals from weapondef_t.hits, so they do not strike again. */
static int next_state;
static const uint8_t oplen[69]={
    2,2,1,1,2,1,2,2,4,4,2,2,0,4,4,4,4,4,2,4,4,3,0,1,2,255,4,0,255,0,3,1,
    1,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,0,1,0,0,1,2,0,2,1,2,4,6,6,2,0,2,2,1,4,0,0
};
enum {
    SC_INIT=0, SC_DEATH=1, SC_GND_ATTACK=2, SC_AIR_ATTACK=3, SC_WALKING=11,
    SC_ALMOST_BUILT=15, SC_STAREDIT_INIT=23,
};
/* Engine groups 2 and 3 move and attack; 4 and 6 are only this game's. */
enum { SC_GROUP_IDLE=0, SC_GROUP_WALK=2, SC_GROUP_ATTACK=3, SC_GROUP_DEATH=4, SC_GROUP_WORK=6 };
static unsigned animation_start(const blob_t *s,unsigned id,int anim) {
    if(s->size<6) return 0;
    size_t table=read_u32_le(s->bytes+2)==0?read_u16_le(s->bytes):0;
    for(size_t at=table;at+4<=s->size;at+=4) {
        unsigned key=read_u16_le(s->bytes+at),off=read_u16_le(s->bytes+at+2);
        if(key==65535) break;
        if(key!=id) continue;
        if(off+8u>s->size||memcmp(s->bytes+off,"SCPE",4)) return 0;
        unsigned type=s->bytes[off+4];
        unsigned count=type<=1?2:type==2?4:type<=13?14:type<=15?16:type<=21?22:type==23?24:
                       type==24?26:type<=29?28:0;
        if(anim<0||(unsigned)anim>=count||off+8u+count*2>s->size) return 0;
        return read_u16_le(s->bytes+off+8+anim*2);
    }
    return 0;
}
/* A turning GRP's playfram argument is a multiple of 17 plus the facing slot.
 * After setfldirect, or off a multiple of 17, it names one picture: those are
 * the extra one-direction frames that follow the turning ones. */
static int script_frame(const spritesheet_t *sheet,bool turns,unsigned arg,bool fixed) {
    if(arg>=(unsigned)sheet->numlumps) return -1;
    if(!turns) return (int)arg;
    int turning=(sheet->numlumps+16)/17;
    return !fixed&&arg%17==0?(int)arg/17:turning+(int)arg;
}
typedef struct { int image, sprite; } sc_trail_t; /* What a death leaves: overlay and remnant. */
/* Returns the last state compiled, or -1. A death or effect ends in S_NULL
 * unless the caller chains a trail onto it. */
/* A script that waits before its first playfram keeps showing frame, the
 * picture the unit had (walking and attacks start from the idle pose). */
static int animation(const blob_t *script,unsigned start,int sprite,int head,bool turns,int group,
                     const spritesheet_t *sheet,sc_trail_t *trail,int frame) {
    if(!start||start>=script->size) return -1;
    bool once=group==SC_GROUP_DEATH,fixed=false,strike=false,struck=false;
    (void)once;
    unsigned at=start,stack[16],sp=0,visited[128]; int ids[128],n=0,last=-1;
    for(int step=0;step<2048 && at<script->size;step++) {
        unsigned here=at,op=script->bytes[at++];
        if(op>=sizeof(oplen)) break;
        unsigned len=oplen[op];
        if(len==255) { if(at>=script->size) break; len=1+script->bytes[at]*2; }
        if(len>script->size-at) break;
        const uint8_t *arg=script->bytes+at; at+=len;
        if(op==0||op==1) { int f=script_frame(sheet,turns,read_u16_le(arg),fixed); if(f<0) break; frame=f; }
        else if(op==0x34) fixed=true;
        else if(op==5||op==6) {
            for(int i=0;i<n;i++) if(visited[i]==here) {
                if(last>=0) states[last].nextstate=group==SC_GROUP_ATTACK?1+sprite*2:ids[i];
                return last;
            }
            if(n==128||next_state>=SC_STATES) break;
            int id=n?next_state++:head;
            int ticks=arg[0]; if(ticks<1) ticks=1;
            /* Native script waits count 24 Hz frames; engine ticks are 30 Hz. */
            ticks=(ticks*RTS_TICRATE+12)/24;
            actionf_p1 action=group==SC_GROUP_ATTACK?(strike?A_Attack:NULL):group==SC_GROUP_WALK?A_Chase:
                              group==SC_GROUP_IDLE?A_Look:NULL;
            if(strike) { strike=false; struck=true; }
            states[id]=(state_t){.sprite=sprite,.frame=frame,.tics=ticks,.group=group,.nextstate=head,.action=action};
            if(last>=0) states[last].nextstate=id;
            visited[n]=here; ids[n++]=id; last=id;
        } else if(op==7) at=read_u16_le(arg);
        else if(op==0x35) { if(sp==16) break; stack[sp++]=at; at=read_u16_le(arg); }
        else if(op==0x36) { if(!sp) break; at=stack[--sp]; }
        else if(op==27||op==28||op==37||op==38||op==40||op==68) strike=!struck;
        else if(trail&&(op==8||op==9||op==13||op==14)&&trail->image<0) trail->image=read_u16_le(arg);
        else if(trail&&(op==15||op==16||op==17||op==19||op==20||op==21||op==66)&&trail->sprite<0)
            trail->sprite=read_u16_le(arg);
        else if(op==0x16) break;
        else if(op==0x30) break;
    }
    if(last<0) {
        if(next_state>=SC_STATES) return -1;
        states[head]=(state_t){.sprite=sprite,.frame=frame,.tics=1,.group=group,.nextstate=head,
            .action=group==SC_GROUP_WALK?A_Chase:group==SC_GROUP_IDLE?A_Look:NULL};
        last=head;
    }
    /* Attack scripts without an attack opcode still strike on their first frame. */
    if(group==SC_GROUP_ATTACK&&!struck) states[head].action=A_Attack;
    if(group==SC_GROUP_ATTACK) states[last].nextstate=1+sprite*2;
    else if(once) states[last].nextstate=S_NULL;
    else { states[last].nextstate=last; states[last].tics=1; }
    return last;
}

/* Death overlays (explosions) and remnants (corpses, rubble) are images.dat
 * entries outside the unit table. Each becomes an extra sprite, decoded once,
 * and plays its Init script after the death that spawns it. The original runs
 * an overlay and the remnant side by side; here they follow one another. */
typedef struct {
    const char *root;
    const blob_t *script,*images,*sprites,*names;
    spritecache_t *cache;
    unsigned ni,ns;
} sc_effects_t;
static sc_effects_t effects;
static int extra_count,extra_image[SC_EXTRA_SPRITES],extra_head[SC_EXTRA_SPRITES],extra_slot[SC_EXTRA_SPRITES];
static char extra_names[SC_EXTRA_SPRITES][16];
static int extra_sprite(const sc_effects_t *e,unsigned image) {
    for(int k=0;k<extra_count;k++) if(extra_image[k]==(int)image) return k;
    if(image>=e->ni||extra_count==SC_EXTRA_SPRITES||e->cache->count>=MAX_DECORATION_SPRITES) return -1;
    const char *grp=sc_tbl_string(e->names,read_u32_le(e->images->bytes+image*4));
    if(!grp) return -1;
    cachedsprite_t *slot=&e->cache->entries[e->cache->count];
    char path[512]; snprintf(path,sizeof(path),"unit/%s",grp);
    blob_t file={0};
    bool decoded=sc_read(e->root,path,&file)&&
        sc_decode_grp(&file,sc_palette,e->images->bytes[e->ni*4+image]!=0,&slot->sprite);
    W_FreeFile(&file);
    if(!decoded) { R_FreeSprite(&slot->sprite); return -1; }
    int k=extra_count++;
    extra_slot[k]=e->cache->count;
    snprintf(extra_names[k],sizeof(extra_names[k]),"sc-img-%03u",image);
    snprintf(slot->name,sizeof(slot->name),"%s",extra_names[k]);
    e->cache->count++;
    sprnames[SC_TYPES+k]=extra_names[k];
    extra_image[k]=(int)image; extra_head[k]=0;
    return k;
}
/* Chains image's Init script after state tail and returns its last state.
 * A shared copy serves every death it ends; one that a remnant must follow
 * is compiled again so the chain stays its own. */
static int effect(const sc_effects_t *e,int tail,unsigned image,bool followed) {
    int k=extra_sprite(e,image);
    if(k<0) return tail;
    if(!followed&&extra_head[k]) { states[tail].nextstate=extra_head[k]; return -1; }
    if(next_state>=SC_STATES) return tail;
    const spritesheet_t *sheet=&e->cache->entries[extra_slot[k]].sprite;
    int head=next_state++;
    int last=animation(e->script,animation_start(e->script,read_u32_le(e->images->bytes+e->ni*10+image*4),SC_INIT),
                       SC_TYPES+k,head,e->images->bytes[e->ni*4+image]!=0,SC_GROUP_DEATH,sheet,NULL,0);
    if(last<0) { next_state--; return tail; }
    states[tail].nextstate=head;
    if(!followed) extra_head[k]=head;
    return last;
}

/* Native subunit1 + images.dat special-overlay LOL coordinates. Layer
 * placement is the difference of authored pivots, never a visual offset. */
static bool compose_turrets(const char *root, const blob_t *units, const blob_t *images,
                           const blob_t *names, const unsigned *image_ids, spritecache_t *cache) {
    unsigned ni=(unsigned)images->size/38;
    for (int i=0;i<SC_TYPES;i++) {
        unsigned sub=read_u16_le(units->bytes+228+i*2);
        if (sub>=SC_TYPES || cache->entries[i].alias || !(sc_units[sub].flags&16)) continue;
        spritesheet_t *body=&cache->entries[i].sprite;
        const spritesheet_t *turret=R_CacheLookup(cache,sc_names[sub]);
        if (!turret) return false;
        unsigned name=read_u32_le(images->bytes+ni*26+image_ids[i]*4);
        const char *lol=sc_tbl_string(names,name);
        blob_t locations={0};
        char path[512];
        if (!lol) return false;
        snprintf(path,sizeof(path),"unit/%s",lol);
        if (!sc_read(root,path,&locations) || locations.size<8) { W_FreeFile(&locations); return false; }
        unsigned nf=read_u32_le(locations.bytes),no=read_u32_le(locations.bytes+4);
        if (!no || nf>(locations.size-8)/4) { W_FreeFile(&locations); return false; }
        int tf=states[1+sub*2].frame;
        if (tf<0 || tf>=turret->spritedef.numframes) { W_FreeFile(&locations); return false; }
        int turning=(body->numlumps+16)/17;
        for (int f=0;f<turning&&f<body->spritedef.numframes;f++) {
            spriteframe_t *frame=&body->spritedef.spriteframes[f];
            const spriteframe_t *turretframe=&turret->spritedef.spriteframes[tf];
            for (int d=0;d<frame->rotations;d++) {
                spritelayer_t *part=frame->directions[d].layers;
                if (!part) continue;
                const spritelayer_t *top=turretframe->directions[d%turretframe->rotations].layers;
                if (!top || part->lump>=nf) { W_FreeFile(&locations); return false; }
                unsigned off=read_u32_le(locations.bytes+8+part->lump*4);
                if (off>locations.size || no*2u>locations.size-off) { W_FreeFile(&locations); return false; }
                const spritecell_t *bc=&body->cells[part->lump],*tc=&turret->cells[top->lump];
                ivec2_t bp=bc->ground_point,tp=tc->ground_point;
                ivec2_t at={(int8_t)locations.bytes[off],(int8_t)locations.bytes[off+1]};
                if (part->flags&RTS_FRAME_FLIP_X) { bp.x=bc->rect.w-bp.x; at.x=-at.x; }
                if (top->flags&RTS_FRAME_FLIP_X) tp.x=tc->rect.w-tp.x;
                spritelayer_t *layers=calloc(3,sizeof(*layers));
                if (!layers) { W_FreeFile(&locations); return false; }
                layers[0]=part[0]; layers[1]=top[0];
                snprintf(layers[1].sprite_name,sizeof(layers[1].sprite_name),"%s",sc_names[sub]);
                layers[1].offset=ivec2_add(ivec2_sub(bp,tp),at);
                free(part); frame->directions[d].layers=layers;
            }
        }
        W_FreeFile(&locations);
    }
    return true;
}


bool sc_load_graphics(const char *root,const level_t *map,spritecache_t *cache) {
    blob_t units={0},flingy={0},sprites={0},images={0},names={0},script={0}; bool ok=false;
    if(!sc_read(root,"arr/units.dat",&units)||!sc_read(root,"arr/flingy.dat",&flingy)||
       !sc_read(root,"arr/sprites.dat",&sprites)||!sc_read(root,"arr/images.dat",&images)||
       !sc_read(root,"arr/images.tbl",&names)||!sc_read(root,"scripts/iscript.bin",&script)) goto done;
    if((units.size!=19192&&units.size!=19876)||flingy.size%15||images.size%38||sprites.size<520||(sprites.size-520)%7) goto done;
    unsigned nf=(unsigned)flingy.size/15,ni=(unsigned)images.size/38;
    /* The original disc has 386 image indices (772 bytes); Stargus's
     * 7-byte row formula is for the expanded Brood War table. */
    unsigned ns=sprites.size==2081?386:130+((unsigned)sprites.size-520)/7;
    next_state=SC_SCRIPT_STATES;
    extra_count=0;
    for(int i=1;i<NUMMOBJTYPES;i++) mobjinfo[i].deathstate=S_NULL;
    int loaded=0; unsigned image_ids[SC_TYPES];
    for(int i=0;i<SC_TYPES;i++) {
        unsigned f=units.bytes[i]; if(f>=nf) goto done;
        unsigned s=read_u16_le(flingy.bytes+f*2); if(s>=ns) goto done;
        unsigned im=read_u16_le(sprites.bytes+s*2); if(im>=ni) goto done;
        image_ids[i]=im;
        unsigned name=read_u32_le(images.bytes+im*4);
        const char *grp=sc_tbl_string(&names,name); if(!grp) goto done;
        cachedsprite_t *slot=&cache->entries[cache->count];
        snprintf(slot->name,sizeof(slot->name),"%s",sc_names[i]);
        for(int j=0;j<i;j++) if(image_ids[j]==im) {
            slot->alias=cache->entries[j].alias?cache->entries[j].alias:&cache->entries[j].sprite; break;
        }
        bool turns=images.bytes[ni*4+im]!=0;
        if(!slot->alias) {
            char path[512]; snprintf(path,sizeof(path),"unit/%s",grp); blob_t file={0};
            if(!sc_read(root,path,&file)||!sc_decode_grp(&file,sc_palette,turns,&slot->sprite)) {
                fprintf(stderr,"starcraft: unit %d image %u could not load %s\n",i,im,path); W_FreeFile(&file); goto done;
            }
            W_FreeFile(&file); ++loaded;
        }
        cache->count++;
        const spritesheet_t *sheet=slot->alias?slot->alias:&slot->sprite;
        unsigned scriptid=read_u32_le(images.bytes+ni*10+im*4);
        unsigned init=animation_start(&script,scriptid,SC_STAREDIT_INIT);
        if(!init) init=animation_start(&script,scriptid,SC_INIT);
        unsigned attack=animation_start(&script,scriptid,SC_GND_ATTACK);
        if(!attack) attack=animation_start(&script,scriptid,SC_AIR_ATTACK);
        animation(&script,init,i,1+i*2,turns,SC_GROUP_IDLE,sheet,NULL,0);
        int pose=states[1+i*2].frame;
        animation(&script,animation_start(&script,scriptid,SC_WALKING),i,2+i*2,turns,SC_GROUP_WALK,sheet,NULL,pose);
        animation(&script,attack,i,1+SC_TYPES*2+i,turns,SC_GROUP_ATTACK,sheet,NULL,pose);
    }
    /* Deaths, after every unit has its cache slot: overlays and remnants are
     * appended past them. Mining loops for the workers. */
    effects=(sc_effects_t){.root=root,.script=&script,.images=&images,.sprites=&sprites,.names=&names,
                           .cache=cache,.ni=ni,.ns=ns};
    for(int i=0;i<SC_TYPES;i++) {
        const spritesheet_t *sheet=cache->entries[i].alias?cache->entries[i].alias:&cache->entries[i].sprite;
        unsigned im=image_ids[i],scriptid=read_u32_le(images.bytes+ni*10+im*4);
        bool turns=images.bytes[ni*4+im]!=0;
        unsigned death=animation_start(&script,scriptid,SC_DEATH);
        if(death&&next_state<SC_STATES) {
            int head=next_state++;
            sc_trail_t trail={-1,-1};
            int tail=animation(&script,death,i,head,turns,SC_GROUP_DEATH,sheet,&trail,states[1+i*2].frame);
            if(tail>=0) {
                mobjinfo[i+1].deathstate=head;
                bool remnant=trail.sprite>=0&&(unsigned)trail.sprite<ns;
                if(trail.image>=0) tail=effect(&effects,tail,(unsigned)trail.image,remnant);
                if(remnant&&tail>=0) effect(&effects,tail,read_u16_le(sprites.bytes+trail.sprite*2),false);
            }
        }
        unsigned mine=animation_start(&script,scriptid,SC_ALMOST_BUILT);
        if((sc_units[i].flags&8)&&mine&&next_state<SC_STATES) {
            int head=next_state++;
            if(animation(&script,mine,i,head,turns,SC_GROUP_WORK,sheet,NULL,states[1+i*2].frame)>=0)
                sc_set_harvest_state(i+1,head);
        }
    }
    game_info.sprite_count=SC_TYPES+extra_count;
    spritesheet_t ramp={0}; char ramp_path[2048];
    snprintf(ramp_path,sizeof(ramp_path),"%s/native/game/tunit.pcx",root);
    if(!W_LoadIndexedSheet(ramp_path,&ramp))goto done;
    for(int i=0;i<cache->count;i++)if(!cache->entries[i].alias) {
        spritesheet_t *sheet=&cache->entries[i].sprite;
        sheet->palette_maps=calloc(8,sizeof(*sheet->palette_maps));
        if(!sheet->palette_maps){R_FreeSprite(&ramp);goto done;}
        sheet->palette_map_count=8;
        for(int team=0;team<8;team++) {
            spritepalettemap_t *map=&sheet->palette_maps[team]; map->id=team;
            for(int k=0;k<256;k++)map->indices[k]=k;
            for(int k=0;k<8;k++)map->indices[8+k]=ramp.lumps[0].indices[team*8+k];
        }
    }
    R_FreeSprite(&ramp);
    ok=compose_turrets(root,&units,&images,&names,image_ids,cache) &&
       sc_load_selection(root,&units,&flingy,&sprites,&images,&names) && R_BindSprites(cache,&game_info);
    printf("StarCraft graphics: %d/228 unit entries, %d unique native GRPs, %d death effect sprites, %d visual states.\n",
           cache->count-extra_count,loaded,extra_count,next_state);
    if(!ok) goto done;
    ok=false;
    const blob_t *chk=map->mission;
    int decoration=0;
    for(size_t at=0;chk&&at<chk->size;) {
        const uint8_t *tag=chk->bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        if(!memcmp(tag,"THG2",4)) for(size_t i=0;i<size;i+=10) {
            const uint8_t *d=data+i;
            if(!(read_u16_le(d+8)&0x1000)) continue;
            unsigned id=read_u16_le(d);
            if(id>=ns) goto done;
            unsigned im=read_u16_le(sprites.bytes+id*2);
            if(im>=ni) goto done;
            const char *grp=sc_tbl_string(&names,read_u32_le(images.bytes+im*4));
            if(!grp) goto done;
            mapdecoration_t *dec=&map->decorations[decoration++];
            snprintf(dec->sprite_name,sizeof(dec->sprite_name),"sc-doodad-%u",im);
            const spritesheet_t *sheet=R_CacheLookup(cache,dec->sprite_name);
            if(!sheet) {
                if(cache->count>=MAX_DECORATION_SPRITES) goto done;
                cachedsprite_t *slot=&cache->entries[cache->count];
                snprintf(slot->name,sizeof(slot->name),"%s",dec->sprite_name);
                char path[512]; snprintf(path,sizeof(path),"unit/%s",grp);
                blob_t file={0};
                bool decoded=sc_read(root,path,&file)&&sc_decode_grp(&file,sc_palette,false,&slot->sprite);
                W_FreeFile(&file);
                if(!decoded) goto done;
                ++cache->count;
                sheet=&slot->sprite;
            }
            ivec2_t pixel={read_u16_le(d+2),read_u16_le(d+4)};
            dec->cell=(ivec2_t){pixel.x/32,pixel.y/32};
            dec->has_sprite_pivot=true;
            /* GRP crop pivot minus the native pixel remainder in this cell. */
            dec->sprite_pivot=ivec2_sub(sheet->cells[0].ground_point,
                                      (ivec2_t){pixel.x%32,pixel.y%32});
            dec->frame2_index=dec->frame3_index=-1;
        }
        at+=8+size;
    }
    ok=true;
done:
    W_FreeFile(&units); W_FreeFile(&flingy); W_FreeFile(&sprites); W_FreeFile(&images); W_FreeFile(&names); W_FreeFile(&script);
    return ok;
}
/* portdata.dat is column-major: idle names, then talking names (u32 tbl ids). */
bool sc_portrait_movie(const char *root,int id,bool talking,char *path,size_t size) {
    blob_t dat={0},tbl={0};bool ok=false;
    if(!sc_read(root,"arr/portdata.dat",&dat)||!sc_read(root,"arr/portdata.tbl",&tbl))goto done;
    unsigned count=(unsigned)(dat.size/12);
    if(id<0||(unsigned)id>=count)goto done;
    const char *name=sc_tbl_string(&tbl,read_u32_le(dat.bytes+(talking?count*4:0)+id*4));
    if(!name)goto done;
    snprintf(path,size,"%s/native/portrait/%s0.smk",root,name);
    for(char *p=path+strlen(root)+1;*p;p++){if(*p=='\\')*p='/';*p=(char)tolower((unsigned char)*p);}
    ok=true;
done:W_FreeFile(&dat);W_FreeFile(&tbl);return ok;
}
bool sc_portrait(const char *root,int id,char *path,size_t size) {
    return sc_portrait_movie(root,id,false,path,size);
}
