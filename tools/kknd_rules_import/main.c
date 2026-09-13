/* Import the supported OpenKrush economy once into authored C rows. No runtime YAML dependency. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int id, faction, category; const char *type, *maker, *path; } entry_t;
static const entry_t entries[] = {
#define S(id,cat,type,maker,path) {id,0,cat,"MT_SURV_" type,"MT_SURV_" maker,"survivors/" path}
#define E(id,cat,type,maker,path) {id,1,cat,"MT_MUTE_" type,"MT_MUTE_" maker,"evolved/" path}
S(1,0,"RIFLEMAN","BARRACKS","infantry/rifleman"),
S(5,0,"SWAT","BARRACKS","infantry/swat"),
S(2,0,"FLAMER","BARRACKS","infantry/flamer"),
S(6,0,"SAPPER","BARRACKS","infantry/sapper"),
S(7,0,"SABOTEUR","BARRACKS","infantry/saboteur"),
S(8,0,"TECHNICIAN","BARRACKS","infantry/technician"),
S(3,0,"RPG_LAUNCHER","BARRACKS","infantry/rpglauncher"),
S(4,0,"SNIPER","BARRACKS","infantry/sniper"),
S(10,1,"DIRT_BIKE","MACHINE_SHOP","vehicles/dirtbike"),
S(11,1,"4X4_PICKUP","MACHINE_SHOP","vehicles/4x4pickup"),
S(15,1,"ATV","MACHINE_SHOP","vehicles/allterrainvehicle"),
S(16,1,"ATV_FLAMETHROWER","MACHINE_SHOP","vehicles/flameatv"),
S(12,1,"ANACONDA_TANK","MACHINE_SHOP","vehicles/anacondatank"),
S(17,1,"BARRAGE_CRAFT","MACHINE_SHOP","vehicles/barragecraft"),
S(13,1,"AUTOCANNON_TANK","MACHINE_SHOP","vehicles/autocannontank"),
S(14,1,"OIL_TANKER","MACHINE_SHOP","vehicles/oiltanker"),
S(18,1,"MOBILE_DERRICK","MACHINE_SHOP","vehicles/derrick"),
S(19,1,"MOBILE_OUTPOST","MACHINE_SHOP","vehicles/mobileoutpost"),
S(20,2,"OUTPOST","OUTPOST","buildings/outpost"),
S(23,2,"BARRACKS","OUTPOST","buildings/barracks"),
S(21,2,"MACHINE_SHOP","OUTPOST","buildings/machineshop"),
S(24,2,"POWER_STATION","OUTPOST","buildings/powerstation"),
S(25,2,"RESEARCH_LAB","OUTPOST","buildings/researchlab"),
S(26,2,"REPAIR_BAY","OUTPOST","buildings/repairbay"),
S(22,3,"GUARD_TOWER","OUTPOST","towers/guardtower"),
S(27,3,"MISSILE_BATTERY","OUTPOST","towers/missilebattery"),
S(28,3,"CANNON_TOWER","OUTPOST","towers/cannontower"),
E(101,0,"BERSERKER","WARRIOR_HALL","infantry/berserker"),
E(103,0,"SHOTGUNNER","WARRIOR_HALL","infantry/shotgunner"),
E(102,0,"PYROMANIAC","WARRIOR_HALL","infantry/pyromaniac"),
E(105,0,"RIOTER","WARRIOR_HALL","infantry/rioter"),
E(106,0,"VANDAL","WARRIOR_HALL","infantry/vandal"),
E(107,0,"MEKANIK","WARRIOR_HALL","infantry/mekanik"),
E(104,0,"BAZOOKA","WARRIOR_HALL","infantry/bazooka"),
E(108,0,"CRAZY_HARRY","WARRIOR_HALL","infantry/crazyharry"),
E(110,1,"DIRE_WOLF","BEAST_ENCLOSURE","vehicles/direwolf"),
E(115,1,"BIKE_SIDECAR","BLACKSMITH","vehicles/bikeandsidecar"),
E(111,1,"MONSTER_TRUCK","BLACKSMITH","vehicles/monstertruck"),
E(112,1,"GIANT_SCORPION","BEAST_ENCLOSURE","vehicles/giantscorpion"),
E(113,1,"WAR_MASTADONT","BEAST_ENCLOSURE","vehicles/warmastodon"),
E(116,1,"GIANT_BEETLE","BEAST_ENCLOSURE","vehicles/giantbeetle"),
E(117,1,"MISSILE_CRAB","BEAST_ENCLOSURE","vehicles/missilecrab"),
E(114,1,"OIL_TANKER","BLACKSMITH","vehicles/oiltanker"),
E(118,1,"MOBILE_DERRICK","BLACKSMITH","vehicles/derrick"),
E(119,1,"CLANHALL_WAGON","BLACKSMITH","vehicles/clanhallwagon"),
E(120,2,"CLANHALL","CLANHALL","buildings/clanhall"),
E(123,2,"WARRIOR_HALL","CLANHALL","buildings/warriorhall"),
E(121,2,"BEAST_ENCLOSURE","CLANHALL","buildings/beastenclosure"),
E(124,2,"BLACKSMITH","CLANHALL","buildings/blacksmith"),
E(125,2,"POWER_STATION","CLANHALL","buildings/powerstation"),
E(126,2,"ALCHEMY_HALL","CLANHALL","buildings/alchemyhall"),
E(127,2,"MENAGERIE","CLANHALL","buildings/menagerie"),
E(122,3,"MACHINEGUN_NEST","CLANHALL","towers/machinegunnest"),
E(128,3,"GRAPESHOT_TOWER","CLANHALL","towers/grapeshotcannon"),
E(129,3,"ROTARY_CANNON","CLANHALL","towers/rotarycannon"),
#undef S
#undef E
};
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"usage: %s OpenKrush-root output.inc\n",argv[0]);return 1;}
    FILE *out=fopen(argv[2],"w"); if(!out)return 1;
    fprintf(out,"/* OpenKrush 76c634d economy. Regenerate with build/kknd_rules_import. */\n");
    for(size_t i=0;i<sizeof(entries)/sizeof(*entries);++i){
        const entry_t *e=&entries[i]; char path[1024],line[1024],label[128]="";
        snprintf(path,sizeof(path),"%s/mods/openkrush_gen1/actors/%s/rules.yaml",argv[1],e->path);
        FILE *in=fopen(path,"r"); if(!in){fprintf(stderr,"missing %s\n",path);return 1;}
        int cost=-1,ticks=-1,level=0,limit=0;
        while(fgets(line,sizeof(line),in)){
            char *p=line;while(*p=='\t'||*p==' ')++p;
            if(!strncmp(p,"Name: ",6)&&!label[0]){snprintf(label,sizeof(label),"%.127s",p+6);label[strcspn(label,"\r\n")]=0;}
            if(!strncmp(p,"Cost: ",6))cost=atoi(p+6);
            if(!strncmp(p,"BuildDuration: ",15))ticks=atoi(p+15);
            if(!strncmp(p,"Level: ",7))level=atoi(p+7);
            if(!strncmp(p,"BuildLimit: ",12))limit=atoi(p+12);
        }
        fclose(in);
        if(cost<0||ticks<0||!label[0]||strchr(label,'"')){fprintf(stderr,"incomplete %s\n",path);return 1;}
        fprintf(out,"KK_PRODUCT(%d, %d, %d, %s, %s, %s, %d, %d, %d, %d, \"%s\")\n",e->id,e->faction,e->category,
            e->category>=2?"RTS_PRODUCT_BUILDING":"RTS_PRODUCT_UNIT",e->type,e->maker,cost,ticks,level,limit,label);
    }
    return fclose(out)!=0;
}
