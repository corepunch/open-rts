/* The engine's own level outcome, for a game that keeps every default: its
 * levels end by the elimination rules and show the plain result screen.
 * The including test defines CHECK and names a unit type of its game. */

static uint16_t outcome_unit_type;

static mobj_t *unit(int owner, uint8_t allegiance) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(8, 8), 0), outcome_unit_type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = allegiance; }
    return u;
}

static void fresh(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 16;
    menuleave = menuactive = false;
    currentmenu = NULL;
}

static void tick(void) {
    mobjlist_t all = P_ListMobjs();
    G_OutcomeTicker(all.items, all.count);
    P_FreeMobjList(&all);
}

/* Players 0 and 2 against player 1. */
static void teams(void) {
    level.player_teams = true;
    for (int a = 0; a < 3; ++a) level.sight.allies[a] = UINT32_C(0x40000000) >> a;
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 2;
    level.sight.allies[2] |= UINT32_C(0x40000000) >> 0;
}

static void kill(mobj_t *victim, mobj_t *killer) { P_DamageMobj(victim, killer, victim->hp); }

static int outcome_cases(uint16_t type) {
    outcome_unit_type = type;
    G_InitGame();
    consoleplayer = 0;

    /* Victory once no hostile side is left; an ally need not fall. */
    fresh();
    mobj_t *me = unit(0, ALLEGIANCE_PLAYER), *foe = unit(1, ALLEGIANCE_ENEMY);
    mobj_t *ally = unit(2, ALLEGIANCE_ALLIED);
    CHECK(me && foe && ally);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_NONE && !menuactive);
    kill(foe, me);
    CHECK(G_SideStats(0)->kills == 1 && G_SideStats(1)->losses == 1 && !G_SideStats(0)->losses);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY && menuactive && currentmenu);
    /* Each side's scenario team is kept for the result, the lost side's too. */
    CHECK(G_SideStats(0)->team == 1 && G_SideStats(1)->team == 2 && G_SideStats(2)->team == 3);
    CHECK(!strcmp(currentmenu->items[1].text, "VICTORY"));
    menuitem_t *next = &currentmenu->items[2];
    next->routine(currentmenu, next, MA_ACTIVATE);
    CHECK(menuleave && !menuactive);
    G_EndLevel(OUTCOME_DEFEAT);
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY && !menuactive); /* decided once */

    /* Defeat when the local side is lost, even if it died before any check. */
    fresh();
    me = unit(0, ALLEGIANCE_PLAYER);
    foe = unit(1, ALLEGIANCE_ENEMY);
    kill(me, foe);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_DEFEAT && menuactive);
    CHECK(!strcmp(currentmenu->items[1].text, "DEFEAT"));
    CHECK(G_SideStats(0)->team == 1); /* noted as it died, before any check */

    /* A game's objectives may leave only some rules to the engine. */
    fresh();
    me = unit(0, ALLEGIANCE_PLAYER);
    foe = unit(1, ALLEGIANCE_ENEMY);
    tick();
    kill(foe, me);
    G_CheckElimination(ELIMINATE_DEFEAT);
    CHECK(G_LevelOutcome() == OUTCOME_NONE);
    G_CheckElimination(ELIMINATE_VICTORY);
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY);

    /* A level with no opponent never ends by elimination. */
    fresh();
    me = unit(0, ALLEGIANCE_PLAYER);
    for (int i = 0; i < 3; ++i) tick();
    CHECK(G_LevelOutcome() == OUTCOME_NONE && !menuactive);

    /* A network match ends on one tic for every peer, once the surviving
     * sides are all allied: here players 0 and 2 against player 1. */
    fresh();
    netgame = true;
    teams();
    me = unit(0, ALLEGIANCE_PLAYER);
    foe = unit(1, ALLEGIANCE_ENEMY);
    ally = unit(2, ALLEGIANCE_ENEMY);
    tick();
    kill(me, foe);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_NONE); /* 1 and 2 still fight */
    kill(foe, ally);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_DEFEAT); /* the match is over; 0 lost */
    fresh();
    teams();
    me = unit(0, ALLEGIANCE_PLAYER);
    foe = unit(1, ALLEGIANCE_ENEMY);
    ally = unit(2, ALLEGIANCE_ENEMY);
    tick();
    kill(foe, me);
    tick();
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY);
    netgame = false;

    P_FreeLevel(&level);
    return 0;
}
