#include "sc_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checked native records with the level, like Doom's map lumps. */
static void free_chk(void *data) {
    sc_mission_t *mission=data;
    free(mission->rt);
    W_FreeFile(&mission->file);
    free(mission);
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

/* Lobby race 0 is Terran, 1 Zerg, 2 Protoss. SIDE bytes are Zerg, Terran, Protoss. */
static int net_race[MAXPLAYERS];
static bool net_race_set;
static int net_seats;
static bool melee_start[MAXPLAYERS], melee_unit[MAXPLAYERS];
static ivec2_t melee_at[MAXPLAYERS];

void sc_set_net_races(const int *races) {
    net_race_set = races != NULL;
    for (int i = 0; i < MAXPLAYERS; ++i) net_race[i] = races ? races[i] : -1;
}

static bool custom_set;
static int custom_kind[8], custom_race[8];

void sc_set_custom_slots(const int *kinds, const int *races) {
    custom_set = kinds && races;
    for (int i = 0; i < 8; ++i) {
        custom_kind[i] = custom_set ? kinds[i] : SC_SLOT_CLOSED;
        custom_race[i] = custom_set ? races[i] : 0;
    }
}

static int side_byte(int race) {
    if (race == 1) return 0;
    if (race == 2) return 2;
    return 1;
}

static uint8_t *chk_chunk(blob_t *file, const char *tag, size_t *size) {
    for (size_t at = 0; at + 8 <= file->size;) {
        uint8_t *rec = file->bytes + at;
        size_t n = read_u32_le(rec + 4);
        if (n > file->size - at - 8) return NULL;
        if (!memcmp(rec, tag, 4)) {
            if (size) *size = n;
            return rec + 8;
        }
        at += 8 + n;
    }
    return NULL;
}

static void note_unit(int seat, unsigned type, ivec2_t pixel, bool decoration) {
    if (seat < 0 || seat >= net_seats || decoration) return;
    if (type + 1 == MT_START_LOCATION) {
        if (!melee_start[seat]) {
            melee_start[seat] = true;
            melee_at[seat] = pixel;
        }
        return;
    }
    melee_unit[seat] = true;
}

/* Playable OWNR slots (human 6 or computer 5) become network seats 0..n-1.
 * A seat whose only placement is a start location is filled in when the
 * things spawn: melee CHK files ship the location and not the buildings. */
static void apply_net_seats(blob_t *file) {
    size_t ownr_n = 0, side_n = 0, forc_n = 0;
    uint8_t *ownr = chk_chunk(file, "OWNR", &ownr_n);
    uint8_t *side = chk_chunk(file, "SIDE", &side_n);
    uint8_t *forc = chk_chunk(file, "FORC", &forc_n);
    if (!ownr || ownr_n < 8 || !doomcom) return;
    int person[8], person_count = 0;
    for (int i = 0; i < 8; ++i)
        if (ownr[i] == 5 || ownr[i] == 6) person[person_count++] = i;
    int seats = doomcom->numplayers > MAXPLAYERS ? MAXPLAYERS : doomcom->numplayers;
    int n = person_count < seats ? person_count : seats;
    if (n < 1) return;
    int dest[8];
    for (int i = 0; i < 8; ++i) dest[i] = -1;
    for (int p = 0; p < n; ++p) dest[person[p]] = p;
    bool taken[8] = {0};
    for (int i = 0; i < 8; ++i) if (dest[i] >= 0) taken[dest[i]] = true;
    int spare[8], spare_n = 0;
    for (int d = 0; d < 8; ++d) if (!taken[d]) spare[spare_n++] = d;
    int spare_at = 0;
    for (int i = 0; i < 8; ++i) if (dest[i] < 0) dest[i] = spare[spare_at++];

    uint8_t old_ownr[8], old_side[8], old_force[8];
    memcpy(old_ownr, ownr, 8);
    if (side && side_n >= 8) memcpy(old_side, side, 8);
    if (forc && forc_n >= 8) memcpy(old_force, forc, 8);
    for (int src = 0; src < 8; ++src) {
        int d = dest[src];
        ownr[d] = old_ownr[src];
        if (side && side_n >= 8) side[d] = old_side[src];
        if (forc && forc_n >= 8) forc[d] = old_force[src];
    }
    for (int p = 0; p < n; ++p) {
        ownr[p] = 6;
        if (side && side_n >= 8) {
            int race = net_race_set ? net_race[p] : -1;
            side[p] = (uint8_t)side_byte(race);
        }
    }
    for (int p = n; p < person_count; ++p) ownr[dest[person[p]]] = 0;
    net_seats = n;

    for (size_t at = 0; at + 8 <= file->size;) {
        uint8_t *rec = file->bytes + at;
        size_t size = read_u32_le(rec + 4);
        if (size > file->size - at - 8) return;
        uint8_t *data = rec + 8;
        if (!memcmp(rec, "UNIT", 4)) {
            for (size_t i = 0; i + 36 <= size; i += 36) {
                uint8_t *u = data + i;
                if (u[16] < 8) u[16] = (uint8_t)dest[u[16]];
                note_unit(u[16], read_u16_le(u + 8),
                          (ivec2_t){read_u16_le(u + 4), read_u16_le(u + 6)}, false);
            }
        } else if (!memcmp(rec, "THG2", 4)) {
            for (size_t i = 0; i + 10 <= size; i += 10) {
                uint8_t *d = data + i;
                if (d[6] < 8) d[6] = (uint8_t)dest[d[6]];
                note_unit(d[6], read_u16_le(d), (ivec2_t){read_u16_le(d + 2), read_u16_le(d + 4)},
                          (read_u16_le(d + 8) & 0x1000) != 0);
            }
        } else if (!memcmp(rec, "TRIG", 4) && size % 2400 == 0) {
            for (size_t t = 0; t < size; t += 2400) {
                for (int a = 0; a < 64; ++a) {
                    uint8_t *v = data + t + 320 + (size_t)a * 32;
                    uint32_t player = read_u32_le(v + 16);
                    if (v[26] != 26 || player >= 8) continue;
                    uint32_t moved = (uint32_t)dest[player];
                    v[16] = (uint8_t)moved;
                    v[17] = v[18] = v[19] = 0;
                }
            }
        }
        at += 8 + size;
    }
}

/* Single-player custom game: the n-th playable OWNR slot takes the n-th
 * choice. Slots keep their native numbers, so the first human stays the
 * console player and the start locations stay with their slots. */
static void apply_custom_slots(blob_t *file) {
    size_t ownr_n = 0, side_n = 0;
    uint8_t *ownr = chk_chunk(file, "OWNR", &ownr_n);
    uint8_t *side = chk_chunk(file, "SIDE", &side_n);
    if (!ownr || ownr_n < 8) return;
    bool open[8] = {0};
    for (int i = 0, k = 0; i < 8; ++i) {
        if (ownr[i] != 5 && ownr[i] != 6) continue;
        int kind = custom_kind[k], race = custom_race[k];
        ++k;
        ownr[i] = kind == SC_SLOT_HUMAN ? 6 : kind == SC_SLOT_COMPUTER ? 5 : 0;
        if (!ownr[i]) continue;
        open[i] = true;
        net_race[i] = race;
        if (side && side_n >= 8) side[i] = (uint8_t)side_byte(race);
    }
    net_race_set = true;
    net_seats = 8;
    for (size_t at = 0; at + 8 <= file->size;) {
        uint8_t *rec = file->bytes + at, *data = rec + 8;
        size_t size = read_u32_le(rec + 4);
        if (size > file->size - at - 8) break;
        if (!memcmp(rec, "UNIT", 4))
            for (size_t i = 0; i + 36 <= size; i += 36)
                note_unit(data[i + 16], read_u16_le(data + i + 8),
                          (ivec2_t){read_u16_le(data + i + 4), read_u16_le(data + i + 6)}, false);
        else if (!memcmp(rec, "THG2", 4))
            for (size_t i = 0; i + 10 <= size; i += 10)
                note_unit(data[i + 6], read_u16_le(data + i),
                          (ivec2_t){read_u16_le(data + i + 2), read_u16_le(data + i + 4)},
                          (read_u16_le(data + i + 8) & 0x1000) != 0);
        at += 8 + size;
    }
    for (int i = 0; i < 8; ++i) if (!open[i]) melee_start[i] = false;
}

static void clear_melee(void) {
    memset(melee_start, 0, sizeof(melee_start));
    memset(melee_unit, 0, sizeof(melee_unit));
    net_seats = 0;
}

bool sc_load_chk(const char *path,level_t *out) {
    static const char *const tilesets[]={
        "badlands","platform","install","ashworld","jungle","desert","ice","twilight"
    };
    memset(out,0,sizeof(*out));
    sc_mission_t *mission=calloc(1,sizeof(*mission));
    if(!mission) return false;
    blob_t *file=&mission->file;
    out->mission=mission; out->destroy_mission=free_chk;
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
    clear_melee();
    if(!netgame) {
        net_race_set = false;
        if(custom_set) apply_custom_slots(file);
    } else if(doomcom && doomcom->numplayers >= 2) apply_net_seats(file);
    /* Preserve native player IDs, including Terran 01's human slot 1.
     * A network match already assigned consoleplayer from the seat. */
    if(!netgame&&owners) for(int i=0;i<8;i++) if(owners[i]==6) { consoleplayer=i; break; }
    for(size_t at=0;at<file->size;) {
        const uint8_t *tag=file->bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        /* Elapsed-zero Set Resources is applied before the first tic so the
         * human's starting stock is already in the level. The mission ticker
         * runs the same trigger again; a set is idempotent. */
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
    /* Melee triggers give the starting stock to player group 13, which this
     * loader does not apply. A network seat that still has no minerals gets
     * that stock directly. */
    for(int i=0;i<net_seats;i++)
        if(out->player_resources[i][0]==0&&owners&&(owners[i]==5||owners[i]==6)) out->player_resources[i][0]=50;
    if(!sc_mission_bind(out)) goto bad;
    return true;
bad:
    fprintf(stderr,"starcraft: invalid or unreadable CHK map %s\n",path);
    P_FreeLevel(out);
    return false;
}

mobj_t *sc_spawn_actor(unsigned type,ivec2_t pixel,uint8_t owner) {
    fixed3_t pos={pixel.x*(FIXED_ONE/32),pixel.y*(FIXED_ONE/32),0};
    mobj_t *mo=P_SpawnMobj(pos,(uint16_t)(type+1));
    if(!mo) return NULL;
    mo->owner=mo->team=owner;
    mo->allegiance=sc_allegiance_for(owner);
    mo->sc.guard_hp=mo->hp;
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
            if(type+1==MT_START_LOCATION) continue; /* Start Location is metadata, not an actor. */
            ivec2_t pixel={read_u16_le(u+4),read_u16_le(u+6)};
            mobj_t *mo=sc_spawn_actor(type,pixel,u[16]);
            if(!mo) return count;
            if(read_u16_le(u+14)&2) mo->hp=mo->sc.guard_hp=mo->max_hp*u[17]/100;
            bool minerals=mo->type_id>=MT_MINERAL_FIELD1&&mo->type_id<=MT_MINERAL_FIELD3;
            if(minerals||mo->type_id==MT_VESPENE_GEYSER||mo->type_id==MT_REFINERY||
               mo->type_id==MT_EXTRACTOR||mo->type_id==MT_ASSIMILATOR) {
                resourcevent_t *vents=realloc(level.resource_vents,
                    (size_t)(level.resource_vent_count+1)*sizeof(*vents));
                if(!vents) return count;
                level.resource_vents=vents;
                irect_t bounds=P_MobjCells(mo);
                vents[level.resource_vent_count++]=(resourcevent_t){
                    .cell={bounds.x,bounds.y},.footprint={bounds.w,bounds.h},
                    .attachment=fixed3_xy_to_fvec2(mo->core.position),
                    .amount=(int)read_u32_le(u+20),.rate=8,.active=mo->type_id!=MT_VESPENE_GEYSER,
                    .exhausts_source=minerals,
                    .resource_type=minerals?0:1,.source_id=mo->id};
            }
            ++count;
        }
        if(!memcmp(tag,"THG2",4)) for(size_t i=0;i<size;i+=10) {
            const uint8_t *d=data+i;
            unsigned type=read_u16_le(d);
            if((read_u16_le(d+8)&0x1000)||type+1==MT_START_LOCATION) continue;
            ivec2_t pixel={read_u16_le(d+2),read_u16_le(d+4)};
            if(!sc_spawn_actor(type,pixel,d[6])) return count;
            ++count;
        }
        at+=8+size;
    }
    /* A seat with a start location and no placed unit is a melee start.
     * The building is centred on that pixel and covers 128 by 96; the workers
     * stand in the row below it. Zerg also start with an Overlord above. */
    static const mobjtype_id_t building[] = {MT_COMMAND_CENTER, MT_HATCHERY, MT_NEXUS};
    static const mobjtype_id_t worker[] = {MT_SCV, MT_DRONE, MT_PROBE};
    for(int i=0;i<net_seats;i++) {
        if(!melee_start[i] || melee_unit[i]) continue;
        int race = net_race_set ? net_race[i] : 0;
        if(race < 0 || race > 2) race = 0;
        ivec2_t pixel = melee_at[i];
        if(!sc_spawn_actor(building[race]-1, pixel, (uint8_t)i)) { clear_melee(); return count; }
        ++count;
        for(int w=0;w<4;w++) {
            ivec2_t at_px = {pixel.x + 48 + w * 24, pixel.y + 64};
            if(!sc_spawn_actor(worker[race]-1, at_px, (uint8_t)i)) { clear_melee(); return count; }
            ++count;
        }
        if(race == 1) {
            if(!sc_spawn_actor(MT_OVERLORD-1, (ivec2_t){pixel.x, pixel.y - 80}, (uint8_t)i)) { clear_melee(); return count; }
            ++count;
        }
    }
    clear_melee();
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

static int brief_string(sc_briefing_t *out, size_t *used, const uint8_t *strings, size_t size, unsigned id) {
    if (!id || size < 2 || id > read_u16_le(strings) || 2u + id * 2u > size) return -1;
    unsigned off = read_u16_le(strings + id * 2);
    if (off >= size || !memchr(strings + off, 0, size - off)) return -1;
    size_t n = strlen((const char *)strings + off) + 1;
    if (n > sizeof(out->strings) - *used) return -1;
    memcpy(out->strings + *used, strings + off, n);
    int at = (int)*used;
    *used += n;
    return at;
}

bool sc_briefing_script(const char *path, sc_briefing_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t b = {0};
    if (!W_ReadFile(path, &b)) return false;
    const uint8_t *strings = NULL, *brief = NULL;
    size_t strings_size = 0, brief_size = 0, used = 0;
    for (size_t p = 0; p + 8 <= b.size;) {
        size_t n = read_u32_le(b.bytes + p + 4);
        if (n > b.size - p - 8) break;
        if (!memcmp(b.bytes + p, "STR ", 4)) { strings = b.bytes + p + 8; strings_size = n; }
        if (!memcmp(b.bytes + p, "MBRF", 4)) { brief = b.bytes + p + 8; brief_size = n; }
        p += 8 + n;
    }
    bool ok = strings && brief && brief_size % 2400 == 0;
    for (size_t t = 0; ok && t < brief_size; t += 2400)
        for (int a = 0; a < 64 && out->count < 64; a++) {
            const uint8_t *v = brief + t + 320 + a * 32;
            if (!v[26]) break;
            if (v[28] & 2) continue; /* disabled */
            out->actions[out->count++] = (sc_brief_action_t){
                .op = v[26], .slot = (uint8_t)read_u32_le(v + 16), .unit = read_u16_le(v + 24),
                .time = (int)read_u32_le(v + 12),
                .text = brief_string(out, &used, strings, strings_size, read_u32_le(v + 4)),
                .wav = brief_string(out, &used, strings, strings_size, read_u32_le(v + 8)),
            };
        }
    W_FreeFile(&b);
    return ok;
}
