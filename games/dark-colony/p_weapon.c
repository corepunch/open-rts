#include "game.h"
#include "info.h"
#include "m_random.h"
#include "p_weapon.h"
#include "p_weapon_data.h"
#include "w_spr.h"
#include "dc_facing.h"
#include <stdlib.h>
#include <string.h>

struct dc_weapons_s { dc_fin_t artillery[2]; };

void DC_FreeWeapons(level_t *map) {
    if (!map->weapons) return;
    for (int i = 0; i < 2; ++i) DC_FreeFIN(&map->weapons->artillery[i]);
    free(map->weapons);
    map->weapons = NULL;
}

bool DC_LoadWeapons(level_t *map, const char *root) {
    DC_FreeWeapons(map);
    map->weapons = calloc(1, sizeof(*map->weapons));
    if (!map->weapons) return false;
    const char *names[] = {"BARR", "ATRIL"};
    for (int i = 0; i < 2; ++i) {
        if (!DC_LoadFIN(M_va("%s/ANIMATE/%s.FIN", root, names[i]), &map->weapons->artillery[i])) {
            DC_FreeWeapons(map);
            return false;
        }
    }
    return true;
}

mobj_t *DC_FireMissiles(mobj_t *source, mobj_t *target, uint16_t type) {
    (void)P_DC_Random(); /* 0x41228d: attack-animation choice, also for one variant. */
    const dc_fin_t *fin = NULL;
    const dc_fin_label_t *label = NULL;
    if (level.weapons && (source->type_id == MT_THUNDERBOLT || source->type_id == MT_ATRIL)) {
        int atril = source->type_id == MT_ATRIL;
        fin = &level.weapons->artillery[atril];
        /* Native facing rounded to sixteen, then animation's eight rotations. */
        int direction = (16 - dc_angle_to_direction(source->core.angle)) & 15;
        direction &= ~1;
        label = DC_FINLabel(fin, M_va("%sFIREA%d", atril ? "ATRIL" : "BARR", direction));
        if (label && (SDL_SwapLE16(label->end) >= SDL_SwapLE16(fin->header->frame_count) ||
                      SDL_SwapLE16(label->start) > SDL_SwapLE16(label->end))) label = NULL;
    }
    mobj_t *first = NULL;
    int elapsed = 0, count = 0;
    if (label) {
        /* 0x423d00 returns up to eight channel-7 launch records. */
        for (int frame = SDL_SwapLE16(label->start); frame <= SDL_SwapLE16(label->end) && count < 8; ++frame) {
            const dc_fin_frame_t *record = &fin->frames[frame];
            const dc_fin_point_t *point = &record->points[7];
            if (point->name[0] && strncmp(point->name, "NONAME", sizeof(point->name))) {
                mobj_t *shot = P_SpawnMissile(source, target, type);
                if (!shot) break;
                fixed3_t offset = {(int16_t)SDL_SwapLE16(point->x) * (8 * 256),
                                  -(int16_t)SDL_SwapLE16(point->y) * (8 * 256), 0};
                shot->core.position = fixed3_add(shot->core.position, offset);
                shot->missile.wait = elapsed * 4;
                if (shot->missile.wait) shot->traits &= ~MF_RENDERABLE;
                DC_ScatterMissile(shot, target->core.position);
                if (!first) first = shot;
                count++;
            }
            int ticks = SDL_SwapLE16(record->ticks);
            elapsed += ((ticks ? ticks : 15) + 3) * 15 / 100;
        }
    }
    if (!count) {
        first = P_SpawnMissile(source, target, type);
        if (first) DC_ScatterMissile(first, target->core.position);
    }
    return first;
}

int DC_WeaponLevel(const mobj_t *actor) {
    if (!actor || actor->native_type_id >= 106 || actor->owner >= 8) return 0;
    int upgrade = level.upgrades[actor->native_type_id][actor->owner].weapon;
    return upgrade <= 2 ? upgrade : 0;
}

int DC_DefendedDamage(const mobj_t *victim, int damage) {
    if (!victim->info || victim->native_type_id >= 106 || victim->owner >= 8) return damage;
    int upgrade = level.upgrades[victim->native_type_id][victim->owner].armor;
    int factor = upgrade <= 2 ? victim->info->defense[upgrade] : 0;
    return factor ? damage * factor / 256 : damage;
}

void DC_AimMissile(mobj_t *missile, fixed3_t destination) {
    /* 0x43d930 / 0x411614: atan ratio, quadrant, then 256 headings. */
    fixed3_t delta = fixed3_sub(destination, missile->core.position);
    int x = delta.x / 256, y = delta.y / 256;
    int ax = abs(x), ay = abs(y), angle = 0;
    if (ax || ay) {
        angle = ax >= ay ? dc_atan[ay * 256 / ax] : 2048 - dc_atan[ax * 256 / ay];
        if (x < 0) angle = 4096 - angle;
        if (y < 0) angle = (8192 - angle) & 8191;
    }
    int heading = angle / 32;
    int quarter = heading / 64, index = heading % 64;
    int sine = dc_sine[index], cosine = dc_sine[64 - index];
    ivec2_t direction;
    switch (quarter) {
        case 0: direction = (ivec2_t){cosine,sine}; break;
        case 1: direction = (ivec2_t){-sine,cosine}; break;
        case 2: direction = (ivec2_t){-cosine,-sine}; break;
        default: direction = (ivec2_t){sine,-cosine}; break;
    }
    int speed = missile->info->missile.step / 256;
    ivec2_t velocity = {direction.x * speed / 2048, direction.y * speed / 2048};
    int duration = abs(velocity.x) > abs(velocity.y) ? x / velocity.x :
                   velocity.y ? y / velocity.y : 0;
    missile->missile.duration = duration;
    missile->core.momentum = (fixed3_t){velocity.x * 256, velocity.y * 256,
        duration ? (delta.z / 256 / duration) * 256 : 0};
    missile->core.angle = (angle_t)heading << 24;
}

void DC_ScatterMissile(mobj_t *missile, fixed3_t destination) {
    if (!missile->info->blast.size) return;
    /* Fire consumes a random byte even for center-only BOOMSTAT rows. */
    unsigned value = P_DC_Random() & 255;
    if (missile->type_id == MT_CANNONBALL || missile->type_id == MT_PUS_BOMB) {
        static const uint8_t weights[] = {3,10,3,10,48,10,3,10,3};
        int index = 0;
        while (index < 9 && value >= weights[index] * 256u / 100u) {
            value -= weights[index] * 256u / 100u;
            index++;
        }
        /* The floored weights total 250. Retail exits with row=column=3
         * for bytes 250..255, producing the unusual (+2,+2) aim offset. */
        ivec2_t offset = index == 9 ? (ivec2_t){2,2} : (ivec2_t){index % 3 - 1,index / 3 - 1};
        destination = fixed3_add(destination,
            (fixed3_t){offset.x * FIXED_ONE, offset.y * FIXED_ONE, 0});
    }
    DC_AimMissile(missile, destination);
}

void A_DC_ArtilleryExplode(mobj_t *actor) {
    /* 0x43e501..0x43e521 selects one BOOMSTAT animation, not both. */
    int state = P_DC_Random() % 2 ? S_GASY1 : S_NUKE1;
    A_Explode(actor);
    P_SetMobjState(actor, state);
}
