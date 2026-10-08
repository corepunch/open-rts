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

bool sc_start_tip(const level_t *map, char *text, size_t size) {
    if (!text || !size) return false;
    text[0] = '\0';
    const blob_t *file = map ? map->mission : NULL;
    if (!file) return false;
    for (size_t at = 0; at + 8 <= file->size;) {
        const uint8_t *tag = file->bytes + at;
        size_t chunk_size = read_u32_le(tag + 4);
        if (chunk_size > file->size - at - 8) return false;
        if (!memcmp(tag, "STR ", 4)) {
            const uint8_t *strings = tag + 8;
            if (chunk_size < 2) return false;
            unsigned count = read_u16_le(strings);
            if (count > (chunk_size - 2) / 2) return false;
            size_t table_size = 2 + (size_t)count * 2;
            for (unsigned id = 1; id <= count; ++id) {
                size_t offset = read_u16_le(strings + id * 2);
                if (offset < table_size || offset >= chunk_size) continue;
                const char *line = (const char *)strings + offset;
                size_t available = chunk_size - offset;
                const char *end = memchr(line, '\0', available);
                if (!end) continue;
                while (line < end && (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')) ++line;
                if (end - line < 3 || memcmp(line, "TIP", 3) ||
                    (line + 3 < end && line[3] != ' ' && line[3] != '\t' && line[3] != '\r' && line[3] != '\n')) continue;
                if (size < 5) return false;
                text[0] = 'T'; text[1] = 'I'; text[2] = 'P'; text[3] = '\n';
                size_t written = 4;
                line += 3;
                while (line < end && (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')) ++line;
                while (line < end && written + 1 < size) {
                    while (line < end && (*line == ' ' || *line == '\t' || *line == '\r')) ++line;
                    while (line < end && *line != '\n' && written + 1 < size)
                        text[written++] = *line++;
                    if (line < end && *line == '\n') {
                        while (written && text[written - 1] == ' ') --written;
                        if (written + 1 >= size) break;
                        text[written++] = '\n';
                        ++line;
                    }
                }
                while (written && (text[written - 1] == '\n' || text[written - 1] == ' ')) --written;
                text[written] = '\0';
                return written > 0;
            }
            return false;
        }
        at += 8 + chunk_size;
    }
    return false;
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
        } else if(!memcmp(tag,"TRIG",4)||!memcmp(tag,"MBRF",4)) {
            if(size%2400) goto bad;
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
        /* Initial elapsed-time triggers supply campaign starting resources.
         * Other conditions/actions require the future mission interpreter. */
        if(!memcmp(tag,"TRIG",4)) for(size_t t=0;t<size;t+=2400) {
            bool initial=true,condition=false;
            for(int c=0;c<16;c++) {
                const uint8_t *v=data+t+c*20;
                if(!v[15])continue;
                condition=true;
                if(v[15]!=22 && !(v[15]==12&&v[14]==0&&read_u32_le(v+8)==0))initial=false;
            }
            if(!initial||!condition)continue;
            for(int a=0;a<64;a++) {
                const uint8_t *v=data+t+320+a*32;
                unsigned player=read_u32_le(v+16),resource=read_u16_le(v+24),amount=read_u32_le(v+20);
                if(v[26]!=26||v[27]!=7||(v[28]&2)||player>=8||resource>2||amount>INT_MAX)continue;
                if(resource==0||resource==2)out->player_resources[player][0]=(int)amount;
                if(resource==1||resource==2)out->player_resources[player][1]=(int)amount;
            }
        }
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
                level.cell_solid[index]=2;
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
            if((type>=176&&type<=178)||type==188||type==110||type==149||type==157) {
                resourcevent_t *vents=realloc(level.resource_vents,
                    (size_t)(level.resource_vent_count+1)*sizeof(*vents));
                if(!vents) return count;
                level.resource_vents=vents;
                irect_t bounds=P_MobjCells(mo);
                vents[level.resource_vent_count++]=(resourcevent_t){
                    .cell={bounds.x,bounds.y},.footprint={bounds.w,bounds.h},
                    .attachment=fixed3_xy_to_fvec2(mo->core.position),
                    .amount=(int)read_u32_le(u+20),.rate=8,.active=type!=188,
                    .exhausts_source=type>=176&&type<=178,
                    .resource_type=type>=176&&type<=178?0:1,.source_id=mo->id};
            }
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

bool sc_briefing(const char *path,char *text,size_t text_size,char *objectives,size_t objectives_size) {
    blob_t b={0};if(!text_size||!objectives_size||!W_ReadFile(path,&b))return false;
    *text=*objectives=0;
    const uint8_t *strings=NULL,*brief=NULL;size_t strings_size=0,brief_size=0;
    bool ok=false;
    for(size_t p=0;p<b.size;) {
        if(b.size-p<8)goto done;
        size_t n=read_u32_le(b.bytes+p+4);if(n>b.size-p-8)goto done;
        if(!memcmp(b.bytes+p,"STR ",4)){strings=b.bytes+p+8;strings_size=n;}
        if(!memcmp(b.bytes+p,"MBRF",4)){brief=b.bytes+p+8;brief_size=n;}
        p+=8+n;
    }
    if(!strings||strings_size<2||2u+read_u16_le(strings)*2u>strings_size||brief_size%2400)goto done;
    for(size_t t=0;t<brief_size;t+=2400)for(int a=0;a<64;a++) {
        const uint8_t *v=brief+t+320+a*32;unsigned id=read_u32_le(v+4);
        if((v[28]&2)||!id||id>read_u16_le(strings)||(v[26]!=4&&v[26]!=8))continue;
        unsigned off=read_u16_le(strings+id*2);
        if(off>=strings_size||!memchr(strings+off,0,strings_size-off))goto done;
        char *dst=v[26]==4?objectives:text;size_t cap=v[26]==4?objectives_size:text_size;
        size_t len=strlen(dst);
        if(len<cap-1)snprintf(dst+len,cap-len,"%s%s",len?"\n\n":"",strings+off);
    }
    ok=true;
done: W_FreeFile(&b);return ok;
}
