#include "t_local.h"
#include "starcraft.h"
#include <stdlib.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static const char *root="data/STARCRAFT";
static char fixture[128];

static int native_map(const char *mission,const char *tileset,int width,int height,bool graphics) {
    char path[1024];
    snprintf(path,sizeof(path),"%s/install/campaign/%s/staredit/scenario.chk",root,mission);
    blob_t chk={0}; CHECK(W_ReadFile(path,&chk));
    CHECK(G_DoLoadLevel(path,&level));
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
            unsigned tile=read_u16_le(data+i*2);
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
        for(int m=0;m<16;m++) walkable|=read_u16_le(raw[3].bytes+tile*32+m*2)&1;
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
    tileset_t tiles={0}; spritesheet_t sprite={0};
    CHECK(!W_LoadAssets(root,&level,"sc-000",&tiles,&sprite)); CHECK(!tiles.indices&&!tiles.tile_lookup);
    P_FreeLevel(&level);
    puts("PASS: reordered/unknown CHK chunks, truncated sections, invalid dimensions, missing MTXM and invalid tile IDs");
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
    CHECK(!malformed()); unlink(fixture);
    SDL_Quit(); return 0;
}
