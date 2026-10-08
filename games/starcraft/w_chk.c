#include "sc_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checked native records with the level, like Doom's map lumps. */
static void free_chk(void *data) {
    blob_t *file=data;
    W_FreeFile(file);
    free(file);
}

bool sc_load_chk(const char *path,level_t *out) {
    static const char *const tilesets[]={
        "badlands","platform","install","ashworld","jungle","desert","ice","twilight"
    };
    memset(out,0,sizeof(*out));
    blob_t *file=calloc(1,sizeof(*file));
    if(!file) return false;
    out->mission=file; out->destroy_mission=free_chk;
    if(!W_ReadFile(path,file)) goto bad;
    const uint8_t *terrain=NULL,*owners=NULL;
    size_t terrain_size=0;
    int era=-1;
    for(size_t at=0;at<file->size;) {
        if(file->size-at<8) goto bad;
        const uint8_t *tag=file->bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        if(size>file->size-at-8) goto bad;
        if(!memcmp(tag,"DIM ",4)) {
            if(size!=4) goto bad;
            out->width=read_u16_le(data); out->height=read_u16_le(data+2);
        } else if(!memcmp(tag,"ERA ",4)) {
            if(size!=2) goto bad;
            era=read_u16_le(data)&7;
        } else if(!memcmp(tag,"MTXM",4)) {
            terrain=data; terrain_size=size;
        } else if(!memcmp(tag,"OWNR",4)) {
            if(size!=12) goto bad;
            owners=data;
        } else if(!memcmp(tag,"UNIT",4)) {
            if(size%36) goto bad;
            for(size_t i=0;i<size;i+=36)
                if(read_u16_le(data+i+8)>=SC_TYPES||data[i+16]>=12) goto bad;
        } else if(!memcmp(tag,"THG2",4)) {
            if(size%10) goto bad;
            for(size_t i=0;i<size;i+=10) {
                if(data[i+6]>=12) goto bad;
                if(read_u16_le(data+i+8)&0x1000) ++out->decoration_count;
                else if(read_u16_le(data+i)>=SC_TYPES) goto bad;
            }
        }
        at+=8+size;
    }
    /* Classic StarEdit supports at most 256x256 32px megatiles. */
    if(era<0||out->width<1||out->height<1||out->width>256||out->height>256) goto bad;
    size_t cells=(size_t)out->width*out->height;
    if(!terrain||terrain_size!=cells*2) goto bad;
    out->tile_ids=malloc(cells*sizeof(*out->tile_ids));
    out->blocked=calloc(cells,1); out->cell_solid=calloc(cells,1);
    if(!out->tile_ids||!out->blocked||!out->cell_solid) goto bad;
    if(out->decoration_count) {
        out->decorations=calloc((size_t)out->decoration_count,sizeof(*out->decorations));
        if(!out->decorations) goto bad;
    }
    for(size_t i=0;i<cells;i++) out->tile_ids[i]=read_u16_le(terrain+i*2);
    snprintf(out->map_path,sizeof(out->map_path),"%s",path);
    snprintf(out->tileset_name,sizeof(out->tileset_name),"%s",tilesets[era]);
    for(int i=0;i<8;i++) out->player_colors[i]=i;
    /* Preserve native player IDs, including Terran 01's human slot 1. */
    if(!netgame&&owners) for(int i=0;i<8;i++) if(owners[i]==6) { consoleplayer=i; break; }
    for(size_t at=0;at<file->size;) {
        const uint8_t *tag=file->bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        if(!memcmp(tag,"UNIT",4)) for(size_t i=0;i<size;i+=36) {
            if(read_u16_le(data+i+8)==214&&data[i+16]==consoleplayer) {
                out->has_camera=true;
                out->camera=fvec2_scale((fvec2_t){read_u16_le(data+i+4),read_u16_le(data+i+6)},1.0f/32);
            }
        }
        at+=8+size;
    }
    return true;
bad:
    fprintf(stderr,"starcraft: invalid or unreadable CHK map %s\n",path);
    P_FreeLevel(out);
    return false;
}

static mobj_t *spawn_thing(unsigned type,ivec2_t pixel,uint8_t owner) {
    fixed3_t pos={pixel.x*(FIXED_ONE/32),pixel.y*(FIXED_ONE/32),0};
    mobj_t *mo=P_SpawnMobj(pos,(uint16_t)(type+1));
    if(!mo) return NULL;
    mo->owner=mo->team=owner;
    mo->allegiance=owner>=8?ALLEGIANCE_NEUTRAL:
        owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_ENEMY;
    mo->core.angle=ANG270;
    if(sc_units[type].flags&1) {
        isize2_t footprint=actor_types[type].footprint;
        ivec2_t origin=ivec2_sub((ivec2_t){pixel.x/32,pixel.y/32},
                               (ivec2_t){footprint.w/2,footprint.h/2});
        for(int y=0;y<footprint.h;y++) for(int x=0;x<footprint.w;x++) {
            ivec2_t cell=ivec2_add(origin,(ivec2_t){x,y});
            if(L_Contains(&level,cell.x,cell.y)) {
                int index=L_Index(&level,cell.x,cell.y);
                level.blocked[index]=level.cell_solid[index]=1;
            }
        }
    }
    return mo;
}

int sc_spawn_things(void) {
    const blob_t *file=level.mission;
    int count=0;
    for(size_t at=0;at<file->size;) {
        const uint8_t *tag=file->bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        if(!memcmp(tag,"UNIT",4)) for(size_t i=0;i<size;i+=36) {
            const uint8_t *u=data+i;
            unsigned type=read_u16_le(u+8);
            if(type==214) continue; /* Start Location is metadata, not an actor. */
            ivec2_t pixel={read_u16_le(u+4),read_u16_le(u+6)};
            mobj_t *mo=spawn_thing(type,pixel,u[16]);
            if(!mo) return count;
            if(read_u16_le(u+14)&2) mo->hp=mo->max_hp*u[17]/100;
            ++count;
        }
        if(!memcmp(tag,"THG2",4)) for(size_t i=0;i<size;i+=10) {
            const uint8_t *d=data+i;
            unsigned type=read_u16_le(d);
            if((read_u16_le(d+8)&0x1000)||type==214) continue;
            ivec2_t pixel={read_u16_le(d+2),read_u16_le(d+4)};
            if(!spawn_thing(type,pixel,d[6])) return count;
            ++count;
        }
        at+=8+size;
    }
    return count;
}
