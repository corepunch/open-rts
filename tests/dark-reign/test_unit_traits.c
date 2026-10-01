#include "t_local.h"
#include "engine.h"
#include "info.h"

#define CHECK(c) RTS_CHECK(c, "Dark Reign traits", #c)

static const mobjtype_t *type_for_native(int id) {
    for (int i = 0; i < num_actor_types; ++i)
        if (mobjinfo[actor_types[i].id].doomednum == id) return &actor_types[i];
    return NULL;
}

static int check_definition(int id, unsigned traits, int *count) {
    const mobjtype_t *type = type_for_native(id);
    if (!type) return 0;
    unsigned mask = MF_FLY | MF_HUMAN | MF_HARVESTER | MF_NOAUTOTARGET;
    if ((type->traits & mask) != traits ||
        (mobjinfo[type->id].flags & mask) != traits) {
        fprintf(stderr, "native=%d name=%s authored=%x generated=%x expected=%x\n",
                id, type->name, type->traits & mask, mobjinfo[type->id].flags & mask, traits);
        return 1;
    }
    ++*count;
    return 0;
}

static int audit_traits(void) {
    FILE *file = fopen("data/REIGN/dark/deftxt/UNITS.TXT", "r");
    CHECK(file);
    char line[512];
    unsigned traits = 0;
    int id = -1, count = 0;
    while (fgets(line, sizeof(line), file)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ';') continue;
        if (!strncmp(p, "DefineUnitType(", 15)) {
            CHECK(check_definition(id, traits, &count) == 0);
            id = -1; traits = 0;
        }
        if (sscanf(p, "SetType(%d)", &id) == 1) continue;
        if (!strncmp(p, "SetMoveMode(Fly)", 16)) traits |= MF_FLY;
        if (!strncmp(p, "IsHuman()", 9)) traits |= MF_HUMAN;
        if (!strncmp(p, "NoAutoTarget()", 14)) traits |= MF_NOAUTOTARGET;
        if (!strncmp(p, "SetResourceTransport(", 21)) traits |= MF_HARVESTER;
    }
    CHECK(check_definition(id, traits, &count) == 0);
    fclose(file);
    CHECK(count > 40);
    printf("PASS: %d native unit definitions match runtime and generated traits\n", count);
    return 0;
}

static mobj_t *spawn(uint16_t type, fvec2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(position, 0), type);
    if (unit) {
        unit->owner = unit->team = 0;
        unit->allegiance = ALLEGIANCE_PLAYER;
    }
    return unit;
}

static int support(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    CHECK(model && rts_game_model_load(model, &config));
    /* Isolate the thinker behavior from scenario units and fog. */
    P_FreeThinkers();
    P_InitThinkers();
    free(level.sight.cells); level.sight.cells = NULL;
    const uint16_t flyers[] = {MT_FG_SKY_BIKE, MT_FG_OUTRIDER,
        MT_IMP_RECON_SAUCER, MT_IMP_CYCLONE, MT_IMP_SKY_FORTRESS};
    for (unsigned i = 0; i < sizeof(flyers) / sizeof(*flyers); ++i) {
        mobj_t *flyer = spawn(flyers[i], (fvec2_t){40, 40});
        CHECK(flyer && (flyer->traits & MF_FLY));
    }
    mobj_t *medic = spawn(MT_FG_MEDIC, (fvec2_t){20, 20});
    mobj_t *human = spawn(MT_FG_RAIDER, (fvec2_t){20.5f, 20});
    mobj_t *vehicle = spawn(MT_FG_FREIGHTER, (fvec2_t){20, 20.5f});
    CHECK(medic && human && vehicle);
    human->hp = 50; vehicle->hp = 500;
    ticcmd_t heal = {.order = TC_ATTACK,.target = human->id,.count = 1,.units = {medic->id}};
    G_RunTiccmd(0,&heal);
    CHECK(medic->attack.target == human);
    heal.target = vehicle->id;
    G_RunTiccmd(0,&heal);
    CHECK(medic->attack.target == human); /* An invalid support order preserves its target. */
    CHECK(P_Attack(medic));
    CHECK(human->hp == 70 && vehicle->hp == 500);
    CHECK(!P_Attack(medic)); /* Ten native cycles between pulses. */
    medic->attack.cooldown_left_ms = 0;
    human->hp = human->max_hp - 1;
    CHECK(P_Attack(medic) && human->hp == human->max_hp);
    medic->attack.cooldown_left_ms = 0;
    CHECK(!P_Attack(medic)); /* No vehicle healing or full-health targets. */
    human->hp = 50; human->owner = human->team = 1;
    human->allegiance = ALLEGIANCE_ENEMY;
    CHECK(!P_Attack(medic) && human->hp == 50);
    mobj_t *mechanic = spawn(MT_FG_MECHANIC, (fvec2_t){20, 20});
    CHECK(mechanic && P_Attack(mechanic));
    CHECK(vehicle->hp == 505 && human->hp == 50);
    mechanic->attack.cooldown_left_ms = 0;
    vehicle->traits |= MF_FLY;
    CHECK(!P_Attack(mechanic));
    vehicle->traits &= ~MF_FLY;
    /* Ordinary thinkers perform support automatically, including Karoch. */
    mobj_t *karoch = spawn(MT_CIV_KAROCH, (fvec2_t){20, 20});
    CHECK(karoch);
    human->owner = human->team = 0; human->allegiance = ALLEGIANCE_PLAYER;
    for (int i = 0; i < 35; ++i) P_RunThinkers();
    CHECK(human->hp == human->max_hp && vehicle->hp > 505);
    CHECK(!(type_for_native(11)->traits & MF_ATTACK));
    CHECK(!(type_for_native(1005)->traits & MF_ATTACK));
    mobj_t *sniper = spawn(MT_FG_SNIPER, (fvec2_t){20, 20});
    CHECK(sniper);
    human->owner = human->team = 1; human->allegiance = ALLEGIANCE_ENEMY;
    CHECK(!P_Attack(sniper));
    sniper->attack.target = human;
    CHECK(P_Attack(sniper));
    rts_game_model_destroy(model);
    puts("PASS: native support amounts, target classes, cooldown, cap and thinker dispatch");
    return 0;
}

int main(void) {
    RTS_RUN(audit_traits());
    RTS_RUN(support());
    return 0;
}
