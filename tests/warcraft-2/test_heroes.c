#include "t_local.h"
#include "warcraft-2.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II original heroes", #c)

/* Original Tides of Darkness PUD slots 0x31..0x35. */
static const int heroes[] = {MT_CHOGALL, MT_LOTHAR, MT_GULDAN, MT_UTHER_LIGHTBRINGER, MT_ZULJIN};

static void fixture(void) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = 32;
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.cell_terrain = calloc(1024, 1);
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
}

static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

static int effect_count(int kind) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        count += th->function == P_MobjThinker && !u->remove && u->type_id == MT_W2_EFFECT && u->w2.fx.kind == kind;
    }
    return count;
}

static int combat(void) {
    for (int h = 0; h < 5; ++h) {
        fixture();
        int type = heroes[h];
        mobj_t *hero = spawn(type, (fvec2_t){8.5f, 8.5f}, 0);
        mobj_t *enemy = spawn(MT_GRUNT, (fvec2_t){9.5f, 8.5f}, 1);
        enemy->hp = enemy->max_hp = 10000;
        hero->attack.target = enemy;
        CHECK(P_SetMobjState(hero, mobjinfo[type].missilestate));
        int duration = type == MT_GULDAN ? 40 : type == MT_ZULJIN ? 74 : 25;
        for (int tic = 0; tic < duration; ++tic) {
            int frame = type == MT_GULDAN ?
                (tic < 10 ? 5 + tic / 5 : tic < 17 ? 7 : tic < 22 ? 8 : 0) :
                (tic < 9 ? 5 + tic / 3 : tic < (type == MT_ZULJIN ? 21 : 14) ? 8 : 0);
            CHECK(hero->core.frame == frame);
            CHECK(states[hero->core.state_id].group == W2_GROUP_ATTACK);
            if (tic == duration - 1) hero->attack.target = NULL;
            CHECK(P_TickMobjState(hero));
        }
        CHECK(hero->core.state_id == mobjinfo[type].spawnstate);
        if (type == MT_GULDAN || type == MT_ZULJIN) {
            CHECK(effect_count(type == MT_GULDAN ? W2_FX_TOUCH : W2_FX_AXE) == 1);
            for (int tic = 0; tic < 30; ++tic) P_Ticker();
        }
        CHECK(enemy->hp < 10000);
        P_DamageMobj(hero, NULL, hero->hp);
        if (type == MT_GULDAN) {
            for (int tic = 0; tic < 21; ++tic) {
                CHECK(hero->core.frame == (tic < 15 ? 9 + tic / 5 : 12));
                P_TickMobjState(hero);
            }
            CHECK(hero->remove);
        } else CHECK(hero->core.frame == 9);
    }
    return 0;
}

static int roster(const char *path) {
    fixture();
    w2_pud_unit_t records[5];
    for (int i = 0; i < 5; ++i)
        records[i] = (w2_pud_unit_t){.x = 4 + i * 4, .y = 4, .type = 49 + i, .player = 0};
    w2_pud_t pud = {.units = records, .unit_count = 5};
    level.native_data = &pud;
    int count = w2_spawn_units();
    level.native_data = NULL;
    CHECK(count == 5);
    mobjlist_t list = P_ListMobjs();
    spritecache_t cache = {0};
    SDL_Surface *image = path ? SDL_CreateRGBSurfaceWithFormat(0, 432, 360, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (path) { CHECK(image); SDL_FillRect(image, NULL, 0xff304858); }
    const int entries[] = {52, 51, 58, 51, 54}, frames[] = {14, 14, 13, 14, 12};
    for (int i = 0; i < 5; ++i) {
        mobj_t *unit = NULL;
        for (int j = 0; j < list.count; ++j) if (list.items[j]->type_id == heroes[i]) unit = list.items[j];
        CHECK(unit && (mobjinfo[unit->type_id].w2.attributes & W2_HERO));
        CHECK((unit->traits & (MF_MOBILE | MF_ATTACK | MF_SELECTABLE | MF_RENDERABLE)) ==
              (MF_MOBILE | MF_ATTACK | MF_SELECTABLE | MF_RENDERABLE));
        CHECK(unit->hp == mobjinfo[heroes[i]].spawnhealth);
        CHECK(w2_grp_entry(&mobjinfo[heroes[i]], 0, 400) == entries[i]);
        CHECK(w2_cache_unit_sprite("data/WAR2", &cache, heroes[i] - 1));
        const cachedsprite_t *sprite = R_CacheFind(&cache, mobjinfo[heroes[i]].name);
        CHECK(sprite && sprite->sprite.spritedef.numframes == frames[i]);
        for (int f = 0; f < frames[i]; ++f) CHECK(sprite->sprite.spritedef.spriteframes[f].rotations == 8);
        if (image) {
            const int poses[] = {0, 2, 5, heroes[i] == MT_GULDAN ? 7 : 8, 9, 11};
            for (int col = 0; col < 6; ++col) {
                int lump = poses[col] * 5 + 2;
                const spritecell_t *cell = &sprite->sprite.cells[lump];
                CHECK(cell->rect.w <= 72 && cell->rect.h <= 72);
                for (int y = 0; y < cell->rect.h; ++y) for (int x = 0; x < cell->rect.w; ++x) {
                    uint8_t ink = sprite->sprite.lumps[lump].indices[y * cell->rect.w + x];
                    if (!ink) continue;
                    int dx = col * 72 + (72 - cell->rect.w) / 2 + x;
                    int dy = i * 72 + (72 - cell->rect.h) / 2 + y;
                    ((uint32_t *)((uint8_t *)image->pixels + dy * image->pitch))[dx] = sprite->sprite.palette[ink];
                }
            }
        }
        CHECK(!G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, heroes[i]));
        fvec2_t goal = {4.5f + i * 4, 9.5f};
        ticcmd_t cmd = {.order = TC_MOVE, .count = 1, .units = {unit->id}, .position = fixed3_from_fvec2(goal, 0)};
        G_RunTiccmd(0, &cmd);
        for (int t = 0; t < 1000 && P_HasMoveOrder(unit); ++t) P_Ticker();
        CHECK(fvec2_near(fixed3_xy_to_fvec2(unit->core.position), goal, 0.01f));
    }
    R_FreeSpriteCache(&cache); P_FreeMobjList(&list);
    if (image) {
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
        CHECK(rgb && SDL_SaveBMP(rgb, path) == 0);
        SDL_FreeSurface(rgb); SDL_FreeSurface(image);
    }
    return 0;
}

static int cast(mobj_t *unit, int spell, mobj_t *target) {
    unit->w2.mana = 255;
    ticcmd_t cmd = {.order = TC_SPELL, .count = 1, .units = {unit->id}, .product = spell,
        .target = target ? target->id : 0, .position = target ? target->core.position : unit->core.position};
    G_RunTiccmd(unit->owner, &cmd);
    CHECK(unit->w2.cast.spell == spell);
    for (int t = 0; t < 200 && unit->w2.mana == 255; ++t) P_Ticker();
    CHECK(unit->w2.mana < 255);
    return 0;
}

static int spells_and_research(void) {
    fixture();
    mobj_t *chogall = spawn(MT_CHOGALL, (fvec2_t){4.5f, 5.5f}, 0);
    mobj_t *lothar = spawn(MT_LOTHAR, (fvec2_t){6.5f, 5.5f}, 0);
    mobj_t *guldan = spawn(MT_GULDAN, (fvec2_t){4.5f, 10.5f}, 0);
    mobj_t *uther = spawn(MT_UTHER_LIGHTBRINGER, (fvec2_t){6.5f, 10.5f}, 0);
    mobj_t *zuljin = spawn(MT_ZULJIN, (fvec2_t){8.5f, 5.5f}, 0);
    CHECK(chogall->w2.mana == 85 && uther->w2.mana == 85 && guldan->w2.mana == 85);
    CHECK(!lothar->w2.mana && !zuljin->w2.mana);
    CHECK(W2_CanCast(chogall, W2_SPELL_EYE) && W2_CanCast(chogall, W2_SPELL_BLOODLUST) &&
          W2_CanCast(chogall, W2_SPELL_RUNES));
    CHECK(W2_CanCast(uther, W2_SPELL_VISION) && !W2_CanCast(uther, W2_SPELL_HEAL));
    CHECK(W2_CanCast(guldan, W2_SPELL_DEATH_COIL) && !W2_CanCast(guldan, W2_SPELL_HASTE));
    for (int s = 1; s < W2_SPELL_COUNT; ++s) CHECK(!W2_CanCast(lothar, s) && !W2_CanCast(zuljin, s));
    RTS_RUN(cast(chogall, W2_SPELL_BLOODLUST, lothar));
    CHECK(lothar->w2.buffs[W2_BUFF_BLOODLUST] && chogall->w2.mana == 205);
    RTS_RUN(cast(chogall, W2_SPELL_EYE, NULL));
    int eyes = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        eyes += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == MT_EYE_OF_KILROGG;
    CHECK(eyes == 1);
    RTS_RUN(cast(chogall, W2_SPELL_RUNES, NULL));
    CHECK(effect_count(W2_FX_RUNE) == 5);
    RTS_RUN(cast(uther, W2_SPELL_VISION, NULL));
    CHECK(effect_count(W2_FX_SPELL) > 0);
    W2_ApplyUpgrade(0, W2_UPGRADE_HEALING); W2_ApplyUpgrade(0, W2_UPGRADE_EXORCISM);
    uther->hp -= 10;
    RTS_RUN(cast(uther, W2_SPELL_HEAL, uther));
    CHECK(uther->hp == uther->max_hp && effect_count(W2_FX_HEAL) == 1);
    mobj_t *skeleton = spawn(MT_SKELETON, (fvec2_t){8.5f, 10.5f}, 1);
    RTS_RUN(cast(uther, W2_SPELL_EXORCISM, skeleton));
    CHECK(skeleton->hp == 0);
    W2_ApplyUpgrade(0, W2_UPGRADE_HASTE);
    RTS_RUN(cast(guldan, W2_SPELL_HASTE, uther));
    CHECK(uther->w2.buffs[W2_BUFF_HASTE]);
    mobj_t *enemy = spawn(MT_GRUNT, (fvec2_t){6.5f, 12.5f}, 1);
    enemy->hp = enemy->max_hp = 1000;
    RTS_RUN(cast(guldan, W2_SPELL_DEATH_COIL, enemy));
    CHECK(effect_count(W2_FX_TOUCH) > 0);
    for (int t = 0; t < 30; ++t) P_Ticker();
    CHECK(enemy->hp < 1000);
    int sword = W2_PiercingDamage(lothar), axe = W2_PiercingDamage(chogall), thrown = W2_PiercingDamage(zuljin);
    W2_ApplyUpgrade(0, W2_UPGRADE_SWORD1); W2_ApplyUpgrade(0, W2_UPGRADE_AXE1);
    W2_ApplyUpgrade(0, W2_UPGRADE_THROWING_AXE1);
    CHECK(W2_PiercingDamage(lothar) == sword + 2 && W2_PiercingDamage(uther) == sword + 2);
    CHECK(W2_PiercingDamage(chogall) == axe + 2 && W2_PiercingDamage(zuljin) == thrown + 1);
    return 0;
}

int main(int argc, char **argv) {
    RTS_RUN(combat());
    RTS_RUN(roster(argc > 1 ? argv[1] : NULL));
    RTS_RUN(spells_and_research());
    P_FreeLevel(&level);
    puts("PASS: five original Warcraft II heroes load native art, move, fight, cast and receive research");
    return 0;
}
