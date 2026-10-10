#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); return 1; } } while(0)

static void put16(uint8_t *at,unsigned v) { at[0]=(uint8_t)v; at[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *at,uint32_t v) { put16(at,v&0xffff); put16(at+2,v>>16); }

/* UNIx: 228 units as parallel arrays. Marine and Zealot (and a Drone, which
 * has no shields to give) are authored; everything else uses the defaults. */
enum { UNIX_BYTES=3648+130*4, UPGX_BYTES=61+1+61*12, TECX_BYTES=44+44*8, PUNI_BYTES=12*228+228+12*228 };
static void unix_section(uint8_t *d,int hp,int armor) {
    memset(d,0,UNIX_BYTES);
    memset(d,1,228);                                   /* use defaults */
    int ids[3]={MT_MARINE-1,MT_ZEALOT-1,MT_DRONE-1};
    for(int k=0;k<3;k++) {
        int i=ids[k];
        d[i]=0;
        put32(d+228+i*4,(uint32_t)(hp+k)<<8);          /* hit points, 24.8 */
        put16(d+1140+i*2,100+k);                       /* shields */
        d[1596+i]=(uint8_t)(armor+k);
        put16(d+1824+i*2,111+k);                       /* build time */
        put16(d+2280+i*2,77+k);                        /* minerals */
        put16(d+2736+i*2,5+k);                         /* gas */
        /* The weapons these units fire: base damage, then bonus damage per upgrade (130 weapons). */
        int weapon[2]={sc_units[i].ground_weapon,sc_units[i].air_weapon};
        for(int w=0;w<2;w++) if(weapon[w]<100) {
            put16(d+3648+weapon[w]*2,(unsigned)(20+weapon[w]));
            put16(d+3648+130*2+weapon[w]*2,(unsigned)(3+weapon[w]));
        }
    }
}
/* Distinct weapon rows the three authored units fire (a row is overridden once). */
static int authored_weapons(void) {
    int ids[3]={MT_MARINE-1,MT_ZEALOT-1,MT_DRONE-1},seen[6],n=0;
    for(int k=0;k<3;k++) for(int w=0;w<2;w++) {
        int id=w?sc_units[ids[k]].air_weapon:sc_units[ids[k]].ground_weapon,known=0;
        for(int s=0;s<n;s++) known|=seen[s]==id;
        if(id<100&&!known) seen[n++]=id;
    }
    return n;
}
static int sections(void) {
    G_InitGame(); P_InitThinkers();
    static sc_unit_t units[SC_TYPES]; static sc_upgrade_t upgrades[SC_UPGRADES];
    memcpy(units,sc_units,sizeof(units)); memcpy(upgrades,sc_upgrades,sizeof(upgrades));
    static sc_weapon_t weapons[SC_WEAPONS]; static sc_tech_t techs[SC_TECHS];
    memcpy(weapons,sc_weapons,sizeof(weapons)); memcpy(techs,sc_techs,sizeof(techs));
    int marine_hp=sc_units[MT_MARINE-1].hp,zealot_shields=sc_units[MT_ZEALOT-1].shields;
    CHECK(marine_hp==40&&zealot_shields==80&&sc_units[MT_DRONE-1].shields==0);

    uint8_t unix_[UNIX_BYTES],unis[4048],upgx[UPGX_BYTES],tecx[TECX_BYTES],puni[PUNI_BYTES];
    unix_section(unix_,55,3);
    memset(unis,1,sizeof(unis)); unis[MT_MARINE-1]=0;   /* vanilla marine: hp 0 */
    memset(upgx,1,sizeof(upgx)); memset(tecx,1,sizeof(tecx));
    upgx[7]=0;                                          /* upgrade 7: cost and time */
    uint8_t *cols=upgx+62;
    put16(cols+(0*61+7)*2,222); put16(cols+(1*61+7)*2,33);
    put16(cols+(2*61+7)*2,44);  put16(cols+(3*61+7)*2,11);
    put16(cols+(4*61+7)*2,999); put16(cols+(5*61+7)*2,88);
    tecx[3]=0; put16(tecx+44+3*2,150);
    /* PUNI: player 1 has its own list without the marine; players 2 and 3 follow
     * the global list, which bans the ghost. */
    memset(puni,1,12*228+228); memset(puni+12*228+228,0,12*228);
    puni[1*228+MT_MARINE-1]=0;
    puni[12*228+MT_GHOST-1]=0;
    memset(puni+12*228+228+2*228,1,228);
    memset(puni+12*228+228+3*228,1,228);                /* players 2 and 3 use the global list */

    sc_rule_sections_t all={.unix_=unix_,.unix_size=sizeof(unix_),.unis=unis,.unis_size=sizeof(unis),
        .upgx=upgx,.upgx_size=sizeof(upgx),.tecx=tecx,.tecx_size=sizeof(tecx),.puni=puni,.puni_size=sizeof(puni)};
    static rulepatchset_t set; memset(&set,0,sizeof(set));
    /* 3 units * 6 values, 1 upgrade * 6, 1 tech * 4, player 1's marine and the
     * ghosts of players 2 and 3. */
    int added=sc_decode_rules(&all,&set);
    const int weapon_entries=authored_weapons()*2;
    CHECK(weapon_entries>=4&&added==3*6+weapon_entries+6+4+1+2&&set.count==added);
    /* Without the x section the vanilla one is used; short ones are ignored. */
    static rulepatchset_t vanilla; memset(&vanilla,0,sizeof(vanilla));
    sc_rule_sections_t old={.unis=unis,.unis_size=sizeof(unis)};
    CHECK(sc_decode_rules(&old,&vanilla)==6+2);
    sc_rule_sections_t cut={.unix_=unix_,.unix_size=100,.puni=puni,.puni_size=10};
    CHECK(sc_decode_rules(&cut,&vanilla)==0&&sc_decode_rules(&(sc_rule_sections_t){0},&vanilla)==0);
    vanilla.count=0;

    uint32_t clean=R_PatchHash(UINT32_C(2166136261));
    const StaticProductDefinition *marine=G_ModelProductByUIId(NULL,MT_MARINE);
    CHECK(marine&&G_ModelProductAvailable(NULL,1,marine));
    R_PatchApply(&set);
    CHECK(R_PatchHash(UINT32_C(2166136261))!=clean);
    CHECK(sc_units[MT_MARINE-1].hp==55&&sc_units[MT_MARINE-1].armor==3&&sc_units[MT_MARINE-1].build_time==111);
    CHECK(sc_units[MT_MARINE-1].minerals==77&&sc_units[MT_MARINE-1].gas==5);
    CHECK(sc_units[MT_ZEALOT-1].shields==101&&sc_units[MT_ZEALOT-1].hp==56);
    CHECK(sc_units[MT_DRONE-1].shields==0&&sc_units[MT_DRONE-1].hp==57); /* no shield to give */
    CHECK(sc_upgrades[7].minerals==222&&sc_upgrades[7].mineral_factor==33&&sc_upgrades[7].gas==44&&
          sc_upgrades[7].gas_factor==11&&sc_upgrades[7].time==999&&sc_upgrades[7].time_factor==88);
    CHECK(sc_upgrades[8].minerals==upgrades[8].minerals);
    /* Weapon rows reach every unit that fires them, and the actor's attack; TECx lands in the tech table. */
    int mw=sc_units[MT_MARINE-1].ground_weapon;
    CHECK(sc_weapons[mw].damage==20+mw&&sc_weapons[mw].bonus==3+mw);
    CHECK(actor_types[MT_MARINE-1].attack.damage==20+mw);
    CHECK(sc_techs[3].minerals==150&&sc_techs[3].gas==0x0101&&sc_techs[3].time==0x0101&&sc_techs[3].energy==0x0101&&sc_techs[4].minerals==techs[4].minerals);
    /* The engine's actor types and the AI's view follow the new stats. */
    CHECK(actor_types[MT_MARINE-1].max_hp==55&&mobjinfo[MT_MARINE].spawnhealth==55);
    AiUnitInfo zealot; P_AiUnitInfo(NULL,MT_ZEALOT,&zealot);
    CHECK(zealot.hp==56+101);
    /* The catalog prices the marine from the table; PUNI bans units per player. */
    CHECK(G_ModelProductByUIId(NULL,MT_MARINE)->cost==77);
    product_t priced; CHECK(R_ProductByUiId(MT_MARINE,&priced)&&priced.cost[0]==77&&priced.cost[1]==5&&priced.kind==RTS_PRODUCT_UNIT);
    CHECK(!G_ModelProductAvailable(NULL,1,marine)&&G_ModelProductAvailable(NULL,0,marine)&&
          G_ModelProductAvailable(NULL,2,marine));
    const StaticProductDefinition *ghost=G_ModelProductByUIId(NULL,MT_GHOST);
    CHECK(ghost&&!sc_unit_unavailable(0,MT_MARINE)&&sc_unit_unavailable(1,MT_MARINE));
    CHECK(sc_unit_unavailable(2,MT_GHOST)&&sc_unit_unavailable(3,MT_GHOST));

    /* The checked-in tables are the authority: the next level restores them. */
    R_PatchApply(NULL);
    CHECK(R_PatchHash(UINT32_C(2166136261))==clean);
    CHECK(!memcmp(units,sc_units,sizeof(units))&&!memcmp(upgrades,sc_upgrades,sizeof(upgrades)));
    CHECK(!memcmp(weapons,sc_weapons,sizeof(weapons))&&!memcmp(techs,sc_techs,sizeof(techs)));
    CHECK(actor_types[MT_MARINE-1].max_hp==marine_hp&&!sc_unit_unavailable(1,MT_MARINE));
    CHECK(G_ModelProductAvailable(NULL,1,marine));
    return 0;
}

/* The same through a tiny CHK: sections are found in any order. */
static int chk_file(void) {
    uint8_t chk[8*4+4+2+2+2+8+UNIX_BYTES+8+UPGX_BYTES+8+PUNI_BYTES+64];
    size_t n=0;
    #define SECTION(tag,len) do { memcpy(chk+n,tag,4); put32(chk+n+4,(uint32_t)(len)); n+=8; } while(0)
    SECTION("DIM ",4); put16(chk+n,2); put16(chk+n+2,1); n+=4;
    SECTION("ERA ",2); put16(chk+n,0); n+=2;
    SECTION("MTXM",4); put16(chk+n,32); put16(chk+n+2,33); n+=4;
    SECTION("UNIx",UNIX_BYTES); unix_section(chk+n,66,1); n+=UNIX_BYTES;
    SECTION("PUNI",PUNI_BYTES);
    memset(chk+n,1,12*228+228); memset(chk+n+12*228+228,0,12*228);
    chk[n+0*228+MT_FIREBAT-1]=0; n+=PUNI_BYTES;
    char path[128]; snprintf(path,sizeof(path),"/private/tmp/open-rts-sc-rules-%ld.chk",(long)getpid());
    FILE *file=fopen(path,"wb"); CHECK(file&&fwrite(chk,1,n,file)==n&&fclose(file)==0);
    G_InitGame(); P_InitThinkers();
    int stock=sc_units[MT_MARINE-1].hp;
    uint32_t clean=R_PatchHash(UINT32_C(2166136261));
    CHECK(G_DoLoadLevel(path,&level)&&g_rulepatch.count==3*6+1+authored_weapons()*2);
    CHECK(sc_units[MT_MARINE-1].hp==66&&actor_types[MT_MARINE-1].max_hp==66);
    CHECK(sc_unit_unavailable(0,MT_FIREBAT)&&R_PatchHash(UINT32_C(2166136261))!=clean);
    P_FreeLevel(&level);
    /* A map without the sections plays with stock stats and a stock hash. */
    size_t plain=8+4+8+2+8+4;
    FILE *again=fopen(path,"wb"); CHECK(again&&fwrite(chk,1,plain,again)==plain&&fclose(again)==0);
    CHECK(G_DoLoadLevel(path,&level)&&g_rulepatch.count==0);
    CHECK(sc_units[MT_MARINE-1].hp==stock&&!sc_unit_unavailable(0,MT_FIREBAT)&&R_PatchHash(UINT32_C(2166136261))==clean);
    P_FreeLevel(&level); unlink(path);
    return 0;
}
int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO)==0);
    CHECK(!sections());
    CHECK(!chk_file());
    puts("PASS: CHK UNIx, UPGx, TECx and PUNI overlay the checked-in tables, hash into the game slot and revert on the next load");
    SDL_Quit(); return 0;
}
