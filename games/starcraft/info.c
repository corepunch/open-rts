#include "sc_local.h"
#include <stdio.h>
#include <string.h>
const sc_unit_t sc_units[SC_TYPES] = {
#define SC_UNIT(id,name,hp,flags,w,h,sight,orders,race,minerals,gas,portrait,time,damage,range,cooldown) [id] = {name,hp,flags,{w,h},sight,orders,race,minerals,gas,portrait,time,damage,range,cooldown},
#include "units.inc"
#undef SC_UNIT
};
char sc_names[SC_TYPES][16];
const char *sprnames[SC_TYPES];
state_t states[SC_STATES];
mobjinfo_t mobjinfo[NUMMOBJTYPES];
gameinfo_t game_info = {
    .sprnames=sprnames,.sprite_count=SC_TYPES,.states=states,.state_count=SC_STATES,
    .mobjinfo=mobjinfo,.mobj_type_count=NUMMOBJTYPES,.null_state=S_NULL,
    .right_click_orders=true,.radial_sight=true,.select_any=true,.f10_menu=true,.instant_turn=true,
};
void sc_init_info(void) {
    memset(states,0,sizeof(states));
    memset(mobjinfo,0,sizeof(mobjinfo));
    for(int i=0;i<SC_TYPES;i++) {
        snprintf(sc_names[i],sizeof(sc_names[i]),"sc-%03d",i);
        sprnames[i]=sc_names[i];
        int stand=1+i*2,walk=stand+1;
        states[stand]=(state_t){.sprite=i,.tics=1,.action=A_Look,.nextstate=stand};
        states[walk]=(state_t){.sprite=i,.tics=1,.action=A_Chase,.nextstate=walk,.group=2};
        int attack=1+SC_TYPES*2+i;
        states[attack]=(state_t){.sprite=i,.tics=1,.action=A_Attack,.nextstate=stand,.group=3};
        mobjinfo[i+1]=(mobjinfo_t){.doomednum=i,.spawnstate=stand,.seestate=walk,
            .missilestate=attack,.spawnhealth=sc_units[i].hp>0?sc_units[i].hp:1,.radius=8,.mass=100};
    }
}
