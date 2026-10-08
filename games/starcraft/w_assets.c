#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t sc_palette[256];
bool sc_read(const char *root,const char *name,blob_t *out) {
    char path[2048];
    snprintf(path,sizeof(path),"%s/native/%s",root,name);
    for(char *p=path+strlen(root)+1;*p;p++) { if(*p=='\\') *p='/'; *p=(char)tolower((unsigned char)*p); }
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
    int frames=turns?(count+16)/17:count;
    if(!R_InitSpriteDef(out,frames,turns?32:1)) goto bad;
    for(int f=0;f<frames;f++) for(int d=0;d<(turns?32:1);d++) {
        int facing=(32-d)%32,slot=facing<=16?facing:32-facing;
        int lump=turns?f*17+slot:f;
        if(lump>=count) continue;
        if(!R_InstallSpriteLump(out,f,d,lump,turns&&facing>16)) goto bad;
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

static const char *tbl_string(const blob_t *tbl,unsigned index) {
    if(tbl->size<2||!index||index>read_u16_le(tbl->bytes)||2+index*2>tbl->size) return NULL;
    unsigned off=read_u16_le(tbl->bytes+index*2);
    return off<tbl->size&&memchr(tbl->bytes+off,0,tbl->size-off)?(char*)tbl->bytes+off:NULL;
}

/* Compile the visual portion of an IScript path into the shared state table.
 * Sound, combat and overlay opcodes are not simulated by this basic catalog. */
static int next_state;
static const uint8_t oplen[69]={
    2,2,1,1,2,1,2,2,4,4,2,2,0,4,4,4,4,4,2,4,4,4,0,1,2,255,4,0,255,0,3,1,
    1,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,0,1,0,0,1,2,0,2,1,2,4,6,6,2,0,0,2,0,0,1,2
};
static unsigned animation_start(const blob_t *s,unsigned id,int anim) {
    if(s->size<6) return 0;
    size_t table=read_u32_le(s->bytes+2)==0?read_u16_le(s->bytes):0;
    for(size_t at=table;at+4<=s->size;at+=4) {
        unsigned key=read_u16_le(s->bytes+at),off=read_u16_le(s->bytes+at+2);
        if(key==65535) break;
        if(key!=id) continue;
        if(off+8u>s->size||memcmp(s->bytes+off,"SCPE",4)) return 0;
        unsigned type=s->bytes[off+4];
        unsigned count=type<=1?2:type==2?4:type<=13?14:type<=15?15:type<=21?21:type==23?23:type==24?25:type<=29?27:0;
        if(anim<0||(unsigned)anim>=count||off+8u+count*2>s->size) return 0;
        return read_u16_le(s->bytes+off+8+anim*2);
    }
    return 0;
}
static void animation(const blob_t *script,unsigned start,int type,int head,bool turns,int group,int numframes) {
    if(!start||start>=script->size) return;
    unsigned at=start,stack[16],sp=0,visited[128]; int ids[128],n=0,last=-1,frame=0;
    for(int step=0;step<2048 && at<script->size;step++) {
        unsigned here=at,op=script->bytes[at++];
        if(op>=sizeof(oplen)) break;
        unsigned len=oplen[op];
        if(len==255) { if(at>=script->size) break; len=1+script->bytes[at]*2; }
        if(len>script->size-at) break;
        const uint8_t *arg=script->bytes+at; at+=len;
        if(op==0||op==1) { frame=read_u16_le(arg); if(turns) frame/=17; if(frame>=numframes) break; }
        else if(op==5||op==6) {
            for(int i=0;i<n;i++) if(visited[i]==here) { if(last>=0) states[last].nextstate=ids[i]; return; }
            if(n==128||next_state>=SC_STATES) break;
            int id=n?next_state++:head;
            int ticks=arg[0]; if(ticks<1) ticks=1;
            /* Native script waits count 24 Hz frames; engine ticks are 30 Hz. */
            ticks=(ticks*RTS_TICRATE+12)/24;
            states[id]=(state_t){.sprite=type,.frame=frame,.tics=ticks,.group=group,.nextstate=head};
            if(last>=0) states[last].nextstate=id;
            visited[n]=here; ids[n++]=id; last=id;
        } else if(op==7) at=read_u16_le(arg);
        else if(op==0x35) { if(sp==16) break; stack[sp++]=at; at=read_u16_le(arg); }
        else if(op==0x36) { if(!sp) break; at=stack[--sp]; }
        else if(op==0x16||op==0x30) break;
    }
    if(last<0 && frame<numframes) states[head].frame=frame;
    if(last>=0) { states[last].nextstate=last; states[last].tics=-1; }
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
        const char *lol=tbl_string(names,name);
        blob_t locations={0};
        char path[512];
        if (!lol) return false;
        snprintf(path,sizeof(path),"unit/%s",lol);
        if (!sc_read(root,path,&locations) || locations.size<8) { W_FreeFile(&locations); return false; }
        unsigned nf=read_u32_le(locations.bytes),no=read_u32_le(locations.bytes+4);
        if (!no || nf>(locations.size-8)/4) { W_FreeFile(&locations); return false; }
        int tf=states[1+sub*2].frame;
        if (tf<0 || tf>=turret->spritedef.numframes) { W_FreeFile(&locations); return false; }
        for (int f=0;f<body->spritedef.numframes;f++) {
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
    next_state=1+SC_TYPES*2;
    int loaded=0; unsigned image_ids[SC_TYPES];
    for(int i=0;i<SC_TYPES;i++) {
        unsigned f=units.bytes[i]; if(f>=nf) goto done;
        unsigned s=read_u16_le(flingy.bytes+f*2); if(s>=ns) goto done;
        unsigned im=read_u16_le(sprites.bytes+s*2); if(im>=ni) goto done;
        image_ids[i]=im;
        unsigned name=read_u32_le(images.bytes+im*4);
        const char *grp=tbl_string(&names,name); if(!grp) goto done;
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
        unsigned init=animation_start(&script,scriptid,23);
        if(!init) init=animation_start(&script,scriptid,0);
        animation(&script,init,i,1+i*2,turns,0,sheet->spritedef.numframes);
        animation(&script,animation_start(&script,scriptid,11),i,2+i*2,turns,2,sheet->spritedef.numframes);
    }
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
    ok=compose_turrets(root,&units,&images,&names,image_ids,cache) && R_BindSprites(cache,&game_info);
    printf("StarCraft graphics: %d/228 unit entries, %d unique native GRPs, %d visual states.\n",cache->count,loaded,next_state);
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
            const char *grp=tbl_string(&names,read_u32_le(images.bytes+im*4));
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
bool sc_portrait(const char *root,int id,char *path,size_t size) {
    blob_t dat={0},tbl={0};bool ok=false;
    if(!sc_read(root,"arr/portdata.dat",&dat)||!sc_read(root,"arr/portdata.tbl",&tbl))goto done;
    unsigned count=(unsigned)(dat.size/12);
    if(id<0||(unsigned)id>=count)goto done;
    const char *name=tbl_string(&tbl,read_u32_le(dat.bytes+id*4));
    if(!name)goto done;
    snprintf(path,size,"%s/native/portrait/%s0.smk",root,name);
    for(char *p=path+strlen(root)+1;*p;p++){if(*p=='\\')*p='/';*p=(char)tolower((unsigned char)*p);}
    ok=true;
done:W_FreeFile(&dat);W_FreeFile(&tbl);return ok;
}
