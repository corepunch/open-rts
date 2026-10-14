#include "engine.h"

/* How a level ends. A game decides its own objectives (campaign scripts,
 * StarCraft triggers) through G_LevelObjectives and G_EndLevel; whatever it
 * leaves open, the engine decides by elimination, so every custom map ends.
 * The result is the local player's and is drawn by G_ShowOutcome. */

/* A unit that counts for its owner's side, alive or not. */
static bool counts(const mobj_t *unit) {
    return unit && unit->owner < 8 && G_PlayerSide(unit->owner) && G_UnitHoldsSide(unit);
}

void G_EndLevel(outcome_t outcome) {
    if (level.outcome || outcome == OUTCOME_NONE) return;
    level.outcome = outcome;
    G_ShowOutcome(outcome);
}

outcome_t G_LevelOutcome(void) { return level.outcome; }

const sidestats_t *G_SideStats(int owner) {
    return owner >= 0 && owner < 8 ? &level.side_stats[owner] : NULL;
}

/* The local player's first unit in the world, alive or not. */
static const mobj_t *local_member(void) {
    if (!thinkercap.next || consoleplayer < 0 || consoleplayer >= 8) return NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner == consoleplayer && counts(unit)) return unit;
    }
    return NULL;
}

/* A dying unit still marks its side and whether that side is the local
 * player's foe, in case it is gone before the next check. */
void G_RecordKill(const mobj_t *victim, const mobj_t *killer) {
    if (!counts(victim)) return;
    level.side_stats[victim->owner].losses++;
    if (killer && killer->owner < 8 && killer->owner != victim->owner)
        level.side_stats[killer->owner].kills++;
    level.sides |= (uint8_t)(1u << victim->owner);
    const mobj_t *me = victim->owner != consoleplayer ? local_member() : NULL;
    if (me && !P_IsAlly(me, victim)) level.foes |= (uint8_t)(1u << victim->owner);
}

static bool allied(const mobj_t *a, const mobj_t *b) {
    return a->owner == b->owner || (P_IsAlly(a, b) && P_IsAlly(b, a));
}

void G_CheckElimination(unsigned rules) {
    if (level.outcome || consoleplayer < 0 || consoleplayer >= 8 || !thinkercap.next) return;
    /* Every mobj still in the world, dying ones included, makes its owner a
     * side, which stays a side once lost; a live one stands for the side. */
    const mobj_t *member[8] = {0}, *side[8] = {0};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (!counts(unit)) continue;
        if (!member[unit->owner]) member[unit->owner] = unit;
        if (!side[unit->owner] && th->function && !unit->remove && unit->hp > 0)
            side[unit->owner] = unit;
    }
    uint8_t living = 0;
    for (int p = 0; p < 8; ++p) {
        if (member[p]) level.sides |= (uint8_t)(1u << p);
        if (side[p]) living |= (uint8_t)(1u << p);
    }
    /* Remember who is hostile to the local player; a lost side has no unit
     * left to ask. */
    const mobj_t *me = member[consoleplayer];
    for (int p = 0; me && p < 8; ++p)
        if (member[p] && p != consoleplayer && !P_IsAlly(me, member[p]))
            level.foes |= (uint8_t)(1u << p);
    /* The sides are kept for result screens even when no rule applies. */
    if (!rules) return;
    bool mine = living & (1u << consoleplayer);
    if (netgame) {
        if (!(level.sides & ~living)) return; /* nobody has lost yet */
        for (int a = 0; a < 8; ++a)
            for (int b = a + 1; b < 8; ++b)
                if (side[a] && side[b] && !allied(side[a], side[b])) return;
        if (mine ? rules & ELIMINATE_VICTORY : rules & ELIMINATE_DEFEAT)
            G_EndLevel(mine ? OUTCOME_VICTORY : OUTCOME_DEFEAT);
        return;
    }
    if (!mine) {
        if ((level.sides & (1u << consoleplayer)) && (rules & ELIMINATE_DEFEAT))
            G_EndLevel(OUTCOME_DEFEAT);
        return;
    }
    if (level.foes && !(level.foes & living) && (rules & ELIMINATE_VICTORY))
        G_EndLevel(OUTCOME_VICTORY);
}

void G_OutcomeTicker(mobj_t *const *units, int count) {
    if (level.outcome || !level.width) return;
    unsigned rules = G_LevelObjectives(units, count);
    if (!level.outcome) G_CheckElimination(rules);
}

/* ── the engine's result screen, for games without their own ────────────── */

static void leave(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action != MA_ACTIVATE) return;
    menuleave = true;
    M_ClearMenus();
}

static void leave_on_escape(menu_t *menu) { leave(menu, NULL, MA_ACTIVATE); }

static menuitem_t result_items[] = {
    {.kind = MI_STATIC, .visible = true, .rect = {200,170,240,110}, .fill = 0xff0c1216u},
    {.kind = MI_STATIC, .visible = true, .rect = {200,185,240,30}, .align = MALIGN_CENTER},
    {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {220,235,200,30},
     .fill = 0xff18242du, .color = 0xffdce6dcu, .text = "CONTINUE", .align = MALIGN_CENTER,
     .routine = leave},
};
static menu_t result_menu = {.items = result_items,
                             .numitems = sizeof(result_items) / sizeof(*result_items)};

void G_ResultScreen(outcome_t outcome) {
    snprintf(result_items[1].text, sizeof(result_items[1].text), "%s",
             outcome == OUTCOME_VICTORY ? "VICTORY" : outcome == OUTCOME_DRAW ? "DRAW" : "DEFEAT");
    menu_t *menu = M_SimpleControlPanel(&result_menu);
    menu->escape = leave_on_escape;
    if (!menu->app && currentmenu) menu->app = currentmenu->app;
    menuinlevel = true;
    M_SetupNextMenu(menu);
}
