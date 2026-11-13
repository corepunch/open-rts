#include "t_local.h"
#include "starcraft.h"
#include "../../games/starcraft/sc_local.h"
#include <stdlib.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static const char *root="data/STARCRAFT";
static char fixture[128];
unsigned sc_terrain_flags(fixed2_t position);

static int native_map(const char *mission,const char *tileset,int width,int height,bool graphics) {
    char path[1024];
    snprintf(path,sizeof(path),"%s/install/campaign/%s/staredit/scenario.chk",root,mission);
    blob_t chk={0}; CHECK(W_ReadFile(path,&chk));
    CHECK(G_DoLoadLevel(path,&level));
    CHECK(G_LoadLevelData(root,&level));
    CHECK(level.width==width&&level.height==height&&!strcmp(level.tileset_name,tileset));
    tileset_t tiles={0}; spritesheet_t fallback={0};
    CHECK(W_LoadAssets(root,&level,"sc-000",&tiles,&fallback));
    CHECK(P_InitSight());
    int spawned=P_LoadThings(path),expected=0,doodads=0;
    mobjlist_t objects=P_ListMobjs(); CHECK(objects.count==spawned);
    unsigned distinct=0; uint8_t seen[65536]={0};
    for(size_t at=0;at<chk.size;) {
        const uint8_t *tag=chk.bytes+at,*data=tag+8;
        size_t size=read_u32_le(tag+4);
        if(!memcmp(tag,"MTXM",4)) for(size_t i=0;i<size/2;i++) {
            unsigned tile=read_u16_le(data+i*2)&0x7fff;
            CHECK(level.tile_ids[i]==tile); CHECK(tile<(unsigned)tiles.tile_lookup_count);
            if(!seen[tile]) { seen[tile]=1; ++distinct; }
        }
        if(!memcmp(tag,"UNIT",4)) for(size_t i=0;i<size;i+=36) {
            const uint8_t *u=data+i; unsigned type=read_u16_le(u+8);
            if(type==214) {
                if(u[16]==consoleplayer) {
                    CHECK(level.has_camera);
                    CHECK(level.camera.x==read_u16_le(u+4)/32.0f&&level.camera.y==read_u16_le(u+6)/32.0f);
                }
                continue;
            }
            CHECK(expected<spawned);
            const mobj_t *mo=objects.items[expected++];
            CHECK(mo->type_id==type+1&&mo->owner==u[16]&&mo->team==u[16]);
            CHECK(mo->core.position.x==read_u16_le(u+4)*(FIXED_ONE/32));
            CHECK(mo->core.position.y==read_u16_le(u+6)*(FIXED_ONE/32));
        }
        if(!memcmp(tag,"THG2",4)) for(size_t i=0;i<size;i+=10) {
            const uint8_t *d=data+i;
            if(read_u16_le(d+8)&0x1000) { ++doodads; continue; }
            if(read_u16_le(d)==214) continue;
            CHECK(expected<spawned);
            const mobj_t *mo=objects.items[expected++];
            CHECK(mo->type_id==read_u16_le(d)+1&&mo->owner==d[6]);
            CHECK(mo->core.position.x==read_u16_le(d+2)*(FIXED_ONE/32));
            CHECK(mo->core.position.y==read_u16_le(d+4)*(FIXED_ONE/32));
        }
        at+=8+size;
    }
    CHECK(expected==spawned&&level.decoration_count==doodads);
    if(!strcmp(mission,"terran/terran01")) CHECK(spawned==46&&distinct==1134&&consoleplayer==1&&doodads==20);
    /* Independently follow every map cell through CV5 and VX4, including
     * reflected minitiles, and compare every decoded pixel with VR4. */
    const char *ext[]={"cv5","vx4","vr4","vf4","wpe"}; blob_t raw[5]={{0}};
    for(int i=0;i<5;i++) {
        snprintf(path,sizeof(path),"%s/native/tileset/%s.%s",root,tileset,ext[i]);
        CHECK(W_ReadFile(path,&raw[i]));
    }
    unsigned flipped=0,unflipped=0,blocked=0;
    for(size_t i=0;i<(size_t)width*height;i++) {
        unsigned id=level.tile_ids[i];
        unsigned tile=read_u16_le(raw[0].bytes+(id>>4)*52+20+(id&15)*2);
        CHECK(tiles.tile_lookup[id]==(int)tile);
        unsigned walkable=0;
        for(int m=0;m<16;m++) {
            unsigned flags=read_u16_le(raw[3].bytes+tile*32+m*2);
            walkable|=flags&1;
            fixed2_t at=fixed2_scale(fixed2_from_cell((ivec2_t){(int)(i%width)*4+m%4,
                (int)(i/width)*4+m/4}),FIXED_ONE/4);
            unsigned cover=read_u16_le(raw[0].bytes+(id>>4)*52+2)&0x10;
            CHECK(sc_terrain_flags(at)==((flags&6)|cover));
        }
        if(!walkable) { CHECK(level.blocked[i]); ++blocked; }
        if(!seen[id]) continue;
        seen[id]=0;
        for(int y=0;y<32;y++) for(int x=0;x<32;x++) {
            unsigned ref=read_u16_le(raw[1].bytes+tile*32+((y/8)*4+x/8)*2);
            unsigned sx=(ref&1)?7-(x%8):x%8;
            uint8_t pixel=raw[2].bytes[(ref>>1)*64+(y%8)*8+sx];
            CHECK(tiles.indices[tile*1024+y*32+x]==pixel);
            if(ref&1) ++flipped; else ++unflipped;
        }
    }
    CHECK(flipped&&unflipped&&blocked);
    CHECK(!sc_terrain_flags(FIXED2_LIT(-1,0))&&!sc_terrain_flags(fixed2_from_cell((ivec2_t){width,height})));
    for(int i=0;i<256;i++) {
        const uint8_t *p=raw[4].bytes+i*4;
        CHECK(tiles.palette[i]==(0xff000000u|p[0]<<16|p[1]<<8|p[2]));
    }
    for(int i=0;i<5;i++) W_FreeFile(&raw[i]);
    if(graphics) {
        spritecache_t *cache=calloc(1,sizeof(*cache)); CHECK(cache);
        CHECK(R_InitSprites(root,&level,objects.items,objects.count,cache));
        for(int i=0;i<doodads;i++) CHECK(R_CacheLookup(cache,level.decorations[i].sprite_name));
        app_t app={.win={width*32,height*32},.cell={32,32}};
        V_AllocScreen(app.win.w,app.win.h); V_BeginFrame(0xff000000);
        R_DrawLevel(&app,&level,&tiles); R_DrawDecorations(&app,&level,cache);
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormatFrom(screens[0].pixels,
            app.win.w,app.win.h,8,app.win.w,SDL_PIXELFORMAT_INDEX8);
        CHECK(surface);
        SDL_Color colors[256];
        for(int i=0;i<256;i++) colors[i]=(SDL_Color){vpalette[i]>>16,vpalette[i]>>8,vpalette[i],255};
        CHECK(!SDL_SetPaletteColors(surface->format->palette,colors,0,256));
        CHECK(!SDL_SaveBMP(surface,"/private/tmp/starcraft-terran01-terrain.bmp"));
        SDL_FreeSurface(surface); V_FreeScreen();
        R_FreeSpriteCache(cache); free(cache);
    }
    printf("PASS: %s %dx%d %s, %u distinct tiles, %d units, %d doodads; native pixels and VF4 checked\n",mission,width,height,tileset,distinct,spawned,doodads);
    W_FreeFile(&chk); R_FreeTileset(&tiles); P_FreeMobjList(&objects); P_FreeLevel(&level);
    return 0;
}
static bool load_fixture(const uint8_t *bytes,size_t size) {
    FILE *file=fopen(fixture,"wb"); if(!file) return false;
    bool written=fwrite(bytes,1,size,file)==size;
    if(fclose(file)) return false;
    return written&&G_DoLoadLevel(fixture,&level);
}
static size_t append_chunk(uint8_t *out,size_t at,const char tag[4],const void *data,size_t size) {
    memcpy(out+at,tag,4);
    out[at+4]=(uint8_t)size; out[at+5]=(uint8_t)(size>>8);
    out[at+6]=(uint8_t)(size>>16); out[at+7]=(uint8_t)(size>>24);
    memcpy(out+at+8,data,size);
    return at+8+size;
}
static size_t append_research_map_prefix(uint8_t *bytes,uint16_t version) {
    uint8_t ver[2]={(uint8_t)version,(uint8_t)(version>>8)},dimensions[4]={2,0,2,0},era[2]={0,0},owners[12]={6,5,5};
    uint16_t tiles[4]={32,32,32,32};
    size_t at=0;
    at=append_chunk(bytes,at,"VER ",ver,sizeof(ver));
    at=append_chunk(bytes,at,"DIM ",dimensions,sizeof(dimensions));
    at=append_chunk(bytes,at,"ERA ",era,sizeof(era));
    at=append_chunk(bytes,at,"MTXM",tiles,sizeof(tiles));
    at=append_chunk(bytes,at,"OWNR",owners,sizeof(owners));
    return at;
}
static int painted_creep(void) {
    enum { WIDTH=32, HEIGHT=32, CELLS=WIDTH*HEIGHT };
    uint8_t *bytes=calloc(1,64+CELLS*2); CHECK(bytes);
    uint8_t dimensions[4]={WIDTH,0,HEIGHT,0}, era[2]={0,0}, owners[12]={0};
    uint16_t *tiles=calloc(CELLS,sizeof(*tiles)); CHECK(tiles);
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++) {
        tiles[y*WIDTH+x]=32;
        if(x<16)tiles[y*WIDTH+x]|=0x8000;
    }
    size_t at=0;
    at=append_chunk(bytes,at,"DIM ",dimensions,sizeof(dimensions));
    at=append_chunk(bytes,at,"ERA ",era,sizeof(era));
    at=append_chunk(bytes,at,"MTXM",tiles,CELLS*sizeof(*tiles));
    at=append_chunk(bytes,at,"OWNR",owners,sizeof(owners));
    free(tiles);
    CHECK(load_fixture(bytes,at)); free(bytes);
    CHECK(G_LoadLevelData(root,&level));
    sc_mission_t *mission=level.mission;
    CHECK(mission->terrain&&level.tile_ids[10*WIDTH+10]==32);
    CHECK(level.tile_ids[10*WIDTH+20]==32);
    CHECK(G_LoadLevelData(root,&level));
    CHECK(mission->terrain&&level.tile_ids[10*WIDTH+10]==32&&level.tile_ids[10*WIDTH+20]==32);
    bool inside=false,outside=false;
    for(int y=2;y<12&&!inside;y++)for(int x=2;x<12;x++) {
        ivec2_t cell={x,y};
        if(P_CanPlaceBuilding(MT_HATCHERY,cell,NULL)) {
            CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN,cell,NULL));
            inside=true; break;
        }
    }
    for(int y=2;y<30&&!outside;y++)for(int x=18;x<30;x++) {
        ivec2_t cell={x,y};
        if(P_CanPlaceBuilding(MT_HATCHERY,cell,NULL)) {
            CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN,cell,NULL));
            outside=true; break;
        }
    }
    CHECK(inside&&outside);

    isize2_t den_foot=actor_types[MT_HYDRALISK_DEN-1].footprint;
    ivec2_t crossing={16-den_foot.w+1,4};
    CHECK(den_foot.w>1&&P_CanPlaceBuilding(MT_HATCHERY,crossing,NULL));
    CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN,crossing,NULL));

    CHECK(P_InitSight());
    app_t app={0}; AiContext ai={0}; hudtext_t hud={0};
    void *saved=NULL; size_t saved_size=0;
    CHECK(G_SaveGameBlob(&saved,&saved_size,"painted-creep",&app,&ai,&hud));
    P_FreeLevel(&level);
    CHECK(G_DoLoadLevel(fixture,&level)&&G_LoadLevelData(root,&level)&&P_InitSight());
    CHECK(G_LoadGameBlob(saved,saved_size,&app,&ai,&hud));
    free(saved);
    mission=level.mission;
    CHECK(mission->terrain&&level.tile_ids[10*WIDTH+10]==32&&level.tile_ids[10*WIDTH+20]==32);
    CHECK(P_CanPlaceBuilding(MT_HATCHERY,crossing,NULL));
    CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN,crossing,NULL));
    P_FreeLevel(&level);
    puts("PASS: MTXM bit 15 is masked for graphics without granting Zerg creep placement");
    return 0;
}
static int cv5_buildability(void) {
    enum { WIDTH=32, HEIGHT=32, CELLS=WIDTH*HEIGHT };
    uint8_t *bytes=calloc(1,64+CELLS*2); CHECK(bytes);
    uint8_t dimensions[4]={WIDTH,0,HEIGHT,0}, era[2]={0,0}, owners[12]={0};
    uint16_t *tiles=calloc(CELLS,sizeof(*tiles)); CHECK(tiles);
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++)
        tiles[y*WIDTH+x]=x<10?16:x<20?192:32; /* Badlands CV5 groups 1, 12, 2. */
    size_t at=0;
    at=append_chunk(bytes,at,"DIM ",dimensions,sizeof(dimensions));
    at=append_chunk(bytes,at,"ERA ",era,sizeof(era));
    at=append_chunk(bytes,at,"MTXM",tiles,CELLS*sizeof(*tiles));
    at=append_chunk(bytes,at,"OWNR",owners,sizeof(owners));
    free(tiles);
    CHECK(load_fixture(bytes,at)); free(bytes);
    CHECK(G_LoadLevelData(root,&level));
    sc_mission_t *mission=level.mission;
    CHECK(mission->terrain[2*WIDTH+2].cv5&SC_TERRAIN_CREEP);
    CHECK(mission->terrain[2*WIDTH+12].cv5&SC_TERRAIN_UNBUILDABLE);
    CHECK(P_CanPlaceBuilding(MT_HYDRALISK_DEN,(ivec2_t){2,2},NULL));
    CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN,(ivec2_t){22,2},NULL));
    CHECK(!P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){2,10},NULL));
    CHECK(P_CanPlaceBuilding(MT_HATCHERY,(ivec2_t){2,10},NULL));
    CHECK(P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){22,10},NULL));
    CHECK(!P_CanPlaceBuilding(MT_HATCHERY,(ivec2_t){12,16},NULL));
    CHECK(P_CanPlaceBuilding(MT_HATCHERY,(ivec2_t){22,16},NULL));

    mobj_t *geyser=sc_spawn_actor(MT_VESPENE_GEYSER-1,(ivec2_t){15*32,18*32},11); CHECK(geyser);
    irect_t vent=P_MobjCells(geyser);
    const uint16_t refineries[]={MT_REFINERY,MT_EXTRACTOR,MT_ASSIMILATOR};
    for(size_t i=0;i<sizeof(refineries)/sizeof(refineries[0]);i++)
        CHECK(P_CanPlaceBuilding(refineries[i],(ivec2_t){vent.x,vent.y},NULL));
    P_RemoveMobj(geyser);

    mobj_t *hatchery=sc_spawn_actor(MT_HATCHERY-1,(ivec2_t){24*32,23*32},0); CHECK(hatchery);
    sc_creep_initial(hatchery); /* A pre-placed Hatchery starts with its full creep. */
    CHECK(P_CanPlaceBuilding(MT_HATCHERY,(ivec2_t){27,26},NULL));
    CHECK(!P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){27,26},NULL));
    P_RemoveMobj(hatchery);

    sc_terrain_t *partial=&mission->terrain[5*WIDTH+25];
    uint16_t vf4[16]; memcpy(vf4,partial->vf4,sizeof(vf4));
    memset(partial->vf4,0,sizeof(partial->vf4));
    partial->vf4[0]=1; /* A walkable minitile makes this megatile partial. */
    CHECK(!P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){25,5},NULL));
    memcpy(partial->vf4,vf4,sizeof(vf4));
    CHECK(P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){25,5},NULL));
    P_FreeLevel(&level);

    CHECK(G_DoLoadLevel(fixture,&level)); /* Deliberately do not load terrain assets. */
    CHECK(!((sc_mission_t *)level.mission)->terrain);
    hatchery=sc_spawn_actor(MT_HATCHERY-1,(ivec2_t){24*32,23*32},0); CHECK(hatchery);
    sc_creep_initial(hatchery);
    CHECK(!P_CanPlaceBuilding(MT_SUPPLY_DEPOT,(ivec2_t){27,26},NULL));
    CHECK(P_CanPlaceBuilding(MT_HATCHERY,(ivec2_t){27,26},NULL));
    P_FreeLevel(&level);
    puts("PASS: CV5 terrain flags, dynamic creep, refinery exceptions and partial VF4 constrain placement");
    return 0;
}
static int malformed(void) {
    /* Chunks need not be ordered; unknown chunks are skipped. */
    uint8_t bytes[]={
        'M','T','X','M',4,0,0,0,32,0,33,0,
        'J','U','N','K',1,0,0,0,99,
        'E','R','A',' ',2,0,0,0,0,0,
        'D','I','M',' ',4,0,0,0,2,0,1,0
    };
    CHECK(load_fixture(bytes,sizeof(bytes))); CHECK(level.width==2&&level.tile_ids[1]==33); P_FreeLevel(&level);
    CHECK(!load_fixture(bytes,sizeof(bytes)-1)); CHECK(!level.mission&&!level.tile_ids);
    bytes[4]=255; CHECK(!load_fixture(bytes,sizeof(bytes))); bytes[4]=4;
    bytes[39]=0; CHECK(!load_fixture(bytes,sizeof(bytes))); bytes[39]=2;
    bytes[39]=3; CHECK(!load_fixture(bytes,sizeof(bytes))); bytes[39]=2;
    bytes[0]='X'; CHECK(!load_fixture(bytes,sizeof(bytes))); bytes[0]='M';
    bytes[8]=bytes[9]=255; CHECK(load_fixture(bytes,sizeof(bytes)));
    CHECK(!G_LoadLevelData(root,&level));
    tileset_t tiles={0}; spritesheet_t sprite={0};
    CHECK(!W_LoadAssets(root,&level,"sc-000",&tiles,&sprite)); CHECK(!tiles.indices&&!tiles.tile_lookup);
    P_FreeLevel(&level);
    puts("PASS: reordered/unknown CHK chunks, truncated sections, invalid dimensions, missing MTXM and invalid tile IDs");
    return 0;
}
static int research_rules(void) {
    enum { CELLS=4, UPGR=46*12*2+46*2+46*12, PUPX=61*12*2+61*2+61*12,
           PTEC=24*12*2+24*2+24*12, PTEX=44*12*2+44*2+44*12 };
    size_t size=64+4+2+CELLS*2+12+10+UPGR+PUPX+PTEC+PTEX;
    uint8_t *bytes=calloc(1,size); CHECK(bytes);
    uint8_t *upgr=calloc(1,UPGR),*pupx=calloc(1,PUPX),*ptec=calloc(1,PTEC),*ptex=calloc(1,PTEX);
    CHECK(upgr&&pupx&&ptec&&ptex);
    sc_set_map_mode(SC_MAP_USE_MAP_SETTINGS);
    /* UPGR: player 0's explicit cap/start, player 1 uses the global pair. */
    upgr[7]=1; upgr[46*12+7]=1;
    upgr[46*24+7]=2; upgr[46*25+7]=1;
    upgr[46*26+1*46+7]=1;
    /* PUPx overrides player 0 and gives player 1 a three-level global cap. */
    pupx[7]=3; pupx[61*12+7]=2;
    pupx[61*24+7]=3; pupx[61*25+7]=1;
    pupx[61*26+1*61+7]=1;
    /* PTEC enables Siege Mode by default; player 0 is explicitly enabled. */
    ptec[5]=1; ptec[24*12+5]=1; ptec[24*24+5]=1;
    ptec[24*26+2*24+5]=1;
    /* PTEx overrides that restriction, starts owner 0 with Siege Mode, and
     * adds the extended tech rows. */
    ptex[5]=0; ptex[44*12+5]=1; ptex[24]=1;
    ptex[44*24+5]=1; ptex[44*25+5]=1; ptex[44*26+2*44+5]=1;
    size_t at=append_research_map_prefix(bytes,63);
    at=append_chunk(bytes,at,"UPGR",upgr,UPGR);
    at=append_chunk(bytes,at,"PUPx",pupx,PUPX);
    at=append_chunk(bytes,at,"PTEC",ptec,PTEC);
    at=append_chunk(bytes,at,"PTEx",ptex,PTEX);
    CHECK(load_fixture(bytes,at));
    sc_mission_t *mission=level.mission;
    CHECK(mission->upgrade_max_level[0][7]==3&&sc_upgrade_level(0,7)==2);
    CHECK(mission->upgrade_max_level[1][7]==3&&sc_upgrade_level(1,7)==1);
    CHECK(sc_upgrade_offered(0,G_ModelProductByUIId(NULL,sc_upgrade_product(7,3))));
    CHECK(sc_upgrade_offered(1,G_ModelProductByUIId(NULL,sc_upgrade_product(7,2))));
    CHECK(!sc_upgrade_offered(0,G_ModelProductByUIId(NULL,SC_TECH_UI+SC_TECH_SIEGE_MODE)));
    CHECK(sc_tech_available(2,SC_TECH_SIEGE_MODE));
    CHECK(level.upgrades[SC_UPGRADES+SC_TECH_SIEGE_MODE][0].weapon==1);
    CHECK(mission->tech_available[0][24]);
    P_FreeLevel(&level);

    for(unsigned version=59;version<=64;version+=5) {
        at=append_research_map_prefix(bytes,(uint16_t)version);
        at=append_chunk(bytes,at,"UPGR",upgr,UPGR);
        at=append_chunk(bytes,at,"PTEC",ptec,PTEC);
        CHECK(load_fixture(bytes,at));
        CHECK(((sc_mission_t *)level.mission)->research_rules_loaded);
        CHECK(sc_upgrade_level(0,7)==1);
        P_FreeLevel(&level);
    }

    /* Single-player custom game changes seats, but still runs Melee rules. */
    int kinds[8]={SC_SLOT_COMPUTER,SC_SLOT_HUMAN},races[8]={0,1};
    sc_set_map_mode(SC_MAP_MELEE); sc_set_custom_slots(kinds,races);
    CHECK(load_fixture(bytes,at));
    CHECK(((sc_mission_t *)level.mission)->map_mode==SC_MAP_MELEE);
    CHECK(consoleplayer==1);
    CHECK(!((sc_mission_t *)level.mission)->research_rules_loaded);
    CHECK(sc_tech_available(0,SC_TECH_SIEGE_MODE));
    P_FreeLevel(&level);

    /* Switching from a custom single-player selection to network Melee clears
     * pending seat overrides before the next map load. */
    sc_set_custom_slots(kinds,races);
    sc_set_map_mode(SC_MAP_MELEE);
    CHECK(load_fixture(bytes,at));
    CHECK(consoleplayer==0&&((sc_mission_t *)level.mission)->map_mode==SC_MAP_MELEE);
    CHECK(!((sc_mission_t *)level.mission)->research_rules_loaded);
    P_FreeLevel(&level); sc_set_custom_slots(NULL,NULL);

    /* Campaign uses the engine's default setup_info values (0,0,0), for
     * which OpenBW enables use_map_settings. */
    sc_set_map_mode(SC_MAP_CAMPAIGN);
    at=append_research_map_prefix(bytes,63);
    at=append_chunk(bytes,at,"UPGR",upgr,UPGR);
    at=append_chunk(bytes,at,"PTEC",ptec,PTEC);
    CHECK(load_fixture(bytes,at));
    CHECK(((sc_mission_t *)level.mission)->map_mode==SC_MAP_CAMPAIGN);
    CHECK(((sc_mission_t *)level.mission)->research_rules_loaded);
    CHECK(sc_upgrade_level(0,7)==1);
    CHECK(sc_tech_available(0,SC_TECH_SIEGE_MODE));
    CHECK(level.upgrades[SC_UPGRADES+SC_TECH_SIEGE_MODE][0].weapon==1);
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_USE_MAP_SETTINGS);

    /* Brood War revisions ignore base chunks; only their extended sections
     * initialize the per-player levels and research state. */
    at=append_research_map_prefix(bytes,205);
    at=append_chunk(bytes,at,"UPGR",upgr,UPGR);
    at=append_chunk(bytes,at,"PTEC",ptec,PTEC);
    CHECK(load_fixture(bytes,at));
    mission=level.mission;
    CHECK(mission->research_rules_loaded);
    CHECK(sc_upgrade_level(0,7)==0&&mission->upgrade_max_level[0][7]==sc_upgrades[7].max_level);
    CHECK(sc_tech_available(0,SC_TECH_SIEGE_MODE));
    CHECK(level.upgrades[SC_UPGRADES+SC_TECH_SIEGE_MODE][0].weapon==0);
    P_FreeLevel(&level);

    at=append_research_map_prefix(bytes,205);
    at=append_chunk(bytes,at,"PUPx",pupx,PUPX);
    at=append_chunk(bytes,at,"PTEx",ptex,PTEX);
    CHECK(load_fixture(bytes,at));
    mission=level.mission;
    CHECK(mission->upgrade_max_level[0][7]==3&&sc_upgrade_level(0,7)==2);
    CHECK(!sc_tech_available(0,SC_TECH_SIEGE_MODE));
    CHECK(level.upgrades[SC_UPGRADES+SC_TECH_SIEGE_MODE][0].weapon==1);
    P_FreeLevel(&level);

    at=append_research_map_prefix(bytes,206);
    at=append_chunk(bytes,at,"PUPx",pupx,PUPX);
    at=append_chunk(bytes,at,"PTEx",ptex,PTEX);
    CHECK(load_fixture(bytes,at));
    CHECK(((sc_mission_t *)level.mission)->upgrade_max_level[0][7]==3);
    P_FreeLevel(&level);

    at=append_research_map_prefix(bytes,999);
    at=append_chunk(bytes,at,"UPGR",upgr,UPGR);
    at=append_chunk(bytes,at,"PTEC",ptec,PTEC);
    CHECK(load_fixture(bytes,at));
    CHECK(!((sc_mission_t *)level.mission)->research_rules_loaded);
    CHECK(sc_tech_available(0,SC_TECH_SIEGE_MODE));
    P_FreeLevel(&level);

    /* The section size is fixed; reject a truncated restrictions table. */
    at=append_research_map_prefix(bytes,63);
    at=append_chunk(bytes,at,"UPGR",upgr,UPGR-1);
    CHECK(!load_fixture(bytes,at));
    free(bytes);free(upgr);free(pupx);free(ptec);free(ptex);
    puts("PASS: UPGR/PUPx per-player levels and PTEC/PTEx availability/research initialize and constrain research");
    return 0;
}
int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO)==0);
    G_InitGame(); P_InitThinkers();
    snprintf(fixture,sizeof(fixture),"/private/tmp/open-rts-sc-map-%ld.chk",(long)getpid());
    CHECK(!native_map("terran/terran01","badlands",64,64,true));
    CHECK(!native_map("terran/terran04","install",128,128,false));
    CHECK(!native_map("terran/terran05","badlands",96,96,false));
    CHECK(!native_map("zerg/zerg01","jungle",64,64,false));
    CHECK(!native_map("terran/tutorial","platform",64,64,false));
    CHECK(!native_map("zerg/zerg03","ashworld",64,96,false));
    CHECK(!painted_creep());
    CHECK(!cv5_buildability());
    CHECK(!malformed()); unlink(fixture);
    CHECK(!research_rules()); unlink(fixture);
    SDL_Quit(); return 0;
}
