#include "p_special.h"
#include "p_path.h"
#include "info.h"

static void repair(mobj_t *healer) {
    /* 0x412f74: expanding squares, X then Y, air before ground. MBULLET row 7. */
    static const uint16_t factors[] = {256,128,128,128,128,256,128,128,0,512};
    if (healer->ability_charge < 4 || healer->repair_wait || P_HasMoveOrder(healer)) return;
    ivec2_t origin = DC_OccupiedPosition(healer);
    for (int radius = 0; radius <= 7; ++radius)
        for (int x = origin.x - radius; x <= origin.x + radius; ++x)
            for (int y = origin.y - radius; y <= origin.y + radius; ++y)
                for (int layer = 0; layer < 2; ++layer) {
                    mobj_t *target = DC_Occupant((ivec2_t){x,y}, layer == 0, false);
                    if (!target || !target->info || target->owner != healer->owner ||
                        target->hp >= target->max_hp || target->info->armor_class >= 10) continue;
                    int amount = 36 * factors[target->info->armor_class] / 256;
                    if (amount > target->max_hp - target->hp) amount = target->max_hp - target->hp;
                    target->hp += amount;
                    healer->ability_charge = 0;
                    healer->repair_wait = 50; /* 0x4133ca, native ticks. */
                    return;
                }
}

void DC_TickSupport(int64_t clock) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->remove || actor->hp <= 0 ||
            (actor->type_id != MT_MEDI_CRAFT && actor->type_id != MT_ZISP)) continue;
        /* 0x41840c: type +0xf8 adds one every 32 native ticks, capped at 255. */
        if (!(clock & 31) && actor->ability_charge < 255) actor->ability_charge++;
        if (actor->repair_wait) actor->repair_wait--;
        repair(actor);
    }
}
