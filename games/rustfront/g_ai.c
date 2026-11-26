#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* The computer player is the engine's (play/p_ai.c) with Rustfront's faction (rules.c). What is
 * Rustfront's own: it places structures itself, on spaced-out sites near its Command Center, so
 * they rise on scaffolding as the player's do (g_build.c); every structure anchors its defence
 * and is a target for its waves, not only refineries; it keeps its power up and repairs; and
 * it plays at a difficulty (RF_SetAIDifficulty).
 *
 * What it knows under the fog: its picture of your army is only what its units have seen
 * (P_VisibleTo in play/p_ai_doctrine.c), and its units only shoot what they see. Like a
 * classic C&C skirmish AI it knows where your structures stand, so its waves march on your
 * nearest one unscouted, and it notices any enemy inside its base's defence radius. */

/* A site with a free cell all round it, so units still find their way between buildings. */
static bool roomy(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    const mobjtype_t *info = P_ActorType(type);
    if (!info || !P_CanPlaceBuilding(type, cell, builder)) return false;
    for (int y = cell.y - 1; y <= cell.y + info->footprint.h; ++y)
        for (int x = cell.x - 1; x <= cell.x + info->footprint.w; ++x)
            if (!L_Contains(&level, x, y) || level.blocked[L_Index(&level, x, y)]) return false;
    return true;
}

/* The nearest roomy site to the Command Center, searched ring by ring. */
static bool find_site(const mobj_t *hq, uint16_t type, ivec2_t *out) {
    ivec2_t origin = fixed2_cell(fixed3_xy(hq->core.position));
    int limit = level.width > level.height ? level.width : level.height;
    for (int r = 2; r <= limit; ++r)
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (abs(x) != r && abs(y) != r) continue;
                ivec2_t cell = ivec2_add(origin, (ivec2_t){x, y});
                if (roomy(type, cell, hq)) { *out = cell; return true; }
            }
    return false;
}

static mobj_t *command_center(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && unit->owner == owner && RF_IsRole(unit->type_id, RF_ROLE_HQ) &&
            !unit->remove && unit->hp > 0) return unit;
    }
    return NULL;
}

/* Structures go on a site the AI picks; everything else through the catalog. */
static bool purchase(level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || product->product_class != RTS_PRODUCT_BUILDING) return G_AiCatalogPurchase(map, owner, ui_id);
    mobj_t *hq = command_center(owner);
    ivec2_t cell;
    if (!hq || RF_SiteInProgress(owner) || !find_site(hq, (uint16_t)product->product_type, &cell)) return false;
    return G_PlaceProduct(hq, product, cell);
}

/* ── difficulty ─────────────────────────────────────────────────────────────
 * Easy collects 40 % less from each load, keeps a smaller army, attacks later with smaller
 * waves and never repairs. Normal plays the faction as rules.c writes it. Hard collects 30 %
 * more (its only cheat), keeps a larger army and one more defence, attacks sooner and at a
 * smaller edge. The setting is part of the game's setup: every peer sets the same. */
static int difficulty[MAXPLAYERS] = {
    RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL, RF_AI_NORMAL,
};
static const uint16_t INCOME[RF_AI_LEVELS] = {154, 256, 333}; /* 8.8 */

void RF_SetAIDifficulty(int owner, int level_) {
    if (owner < 0 || owner >= MAXPLAYERS) return;
    if (level_ < RF_AI_EASY) level_ = RF_AI_EASY;
    if (level_ > RF_AI_HARD) level_ = RF_AI_HARD;
    difficulty[owner] = level_;
    if (owner < 8 && !D_PlayerIsHuman(owner)) level.income_scale[owner] = INCOME[level_];
}

int RF_AIDifficulty(int owner) {
    return owner >= 0 && owner < MAXPLAYERS ? difficulty[owner] : RF_AI_NORMAL;
}

static int scaled(int value, int percent, int floor) {
    value = value * percent / 100;
    return value < floor ? floor : value;
}

/* The faction's plan (rules.c), bent by the owner's difficulty. */
static bool plan(const level_t *map, int owner, int ai_level, AiPlan *out) {
    if (!R_OwnerPlan(map, owner, ai_level, out)) return false;
    int d = RF_AIDifficulty(owner);
    if (owner < 8) level.income_scale[owner] = INCOME[d];
    AiDoctrine *doctrine = &out->doctrine;
    if (d == RF_AI_EASY) {
        doctrine->army_cap = scaled(doctrine->army_cap, 60, 6);
        doctrine->defenses = doctrine->defenses > 1 ? doctrine->defenses - 1 : 1;
        doctrine->attack_ratio += 40;
        out->wave_interval_ms = out->wave_interval_ms * 3 / 2;
        out->wave_min_size = scaled(out->wave_min_size, 60, 3);
        out->wave_max_size = scaled(out->wave_max_size, 60, out->wave_min_size);
    } else if (d == RF_AI_HARD) {
        doctrine->army_cap = scaled(doctrine->army_cap, 135, 8);
        doctrine->defenses += 1;
        doctrine->attack_ratio = scaled(doctrine->attack_ratio, 85, 50);
        out->wave_interval_ms = out->wave_interval_ms * 4 / 5;
        out->wave_max_size = scaled(out->wave_max_size, 125, out->wave_min_size);
    }
    return true;
}

/* Hard and easy change the plan, so they are levels of their own: a change reloads it. */
static int player_level(const level_t *map, int owner) {
    int base = P_AiLevelNonHuman(map, owner);
    return base == AI_LEVEL_NONE ? base : RF_AIDifficulty(owner) == RF_AI_HARD ? AI_LEVEL_PLUS : AI_LEVEL_NORMAL;
}

/* ── upkeep ─────────────────────────────────────────────────────────────────
 * What the shared AI does not know about Rustfront's base: power and repair. Each think, a side
 * short of power (or within 15 of it) builds power before anything else, and, above easy, a
 * side with credits to spare repairs a structure under 60 %. */
static void tactics(level_t *map, int owner, mobj_t *const *units, int unit_count) {
    mobj_t *hq = command_center(owner);
    if (!hq) return;
    int made, used;
    RF_OwnerPower(owner, &made, &used);
    uint16_t power = RF_Role(RF_FactionOfType(hq->type_id), RF_ROLE_POWER);
    if (used + 15 > made && !RF_SiteInProgress(owner)) {
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, power);
        if (p && R_CanAfford(owner, p)) purchase(map, owner, power);
    }
    if (RF_AIDifficulty(owner) == RF_AI_EASY || map->player_resources[owner][0] < 600) return;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *u = units[i];
        if (u->owner != owner || u->remove || u->hp <= 0 || !RF_IsStructure(u) || RF_IsSite(u) ||
            u->rf_repairing || u->hp * 5 >= u->max_hp * 3) continue;
        u->rf_repairing = 1;
        break;
    }
}

static const AiGameInterface rf_ai = {
    .name = "rustfront",
    .features = AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_DEFENSE |
                AI_FEATURE_ATTACK | AI_FEATURE_DOCTRINE,
    .player_level = player_level,
    .plan = plan,
    .purchase = purchase,
    .is_anchor = G_AiIsStructure,
    .tactics = tactics,
};

const AiGameInterface *G_AiInterface(void) { return &rf_ai; }
