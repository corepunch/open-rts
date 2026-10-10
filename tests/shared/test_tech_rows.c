#include "engine.h"
#include "t_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

/* The planner on rows no game ships: a synthetic ruleset gives one product an
 * upgrade level and a tech level to reach, on top of its catalog. The steps
 * come out upgrade first (through the product that provides it), then the
 * tech, which is raised on a producer that already stands. */
static int fake_level, fake_tech, provider_ui;
static int fake_upgrade_level(int owner, int upgrade) { (void)owner; (void)upgrade; return fake_level; }
static int fake_upgrade_product(int upgrade, int level) { (void)upgrade; (void)level; return provider_ui; }
static int fake_tech_level(const mobj_t *producer) { (void)producer; return fake_tech; }
static bool fake_research(level_t *map, mobj_t *producer) { (void)map; (void)producer; ++fake_tech; return true; }

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    CHECK(model && rts_game_model_load(model, &config));
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
    CHECK(level.blocked);

    /* Every maker the catalog names stands for owner 1, so no product lacks a producer. */
    StaticProductDefinition list[512];
    int count = G_ModelGetProducts(NULL, 1, list, 512), spawned = 0;
    mobj_t *maker = NULL;
    for (int i = 0; i < count; ++i)
        for (int m = 0; m < list[i].maker_count; ++m) {
            int type = list[i].makers[m];
            if (type <= 0 || type >= gameinfo->mobj_type_count || G_ModelHasActorType(NULL, 1, (uint16_t)type)) continue;
            mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(fixed2_from_fvec2((fvec2_t){10.0f + 3.0f * (float)spawned++, 40}), 0), (uint16_t)type);
            CHECK(unit);
            unit->owner = unit->team = 1;
            if (!maker) maker = unit;
        }
    CHECK(maker);
    /* A product with nothing missing, to put rows on, and another to provide the upgrade. */
    const StaticProductDefinition *wanted = NULL, *provider = NULL;
    for (int i = 0; i < count && !provider; ++i) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, list[i].ui_id);
        if (product->product_class == RTS_PRODUCT_UPGRADE || product->maker_count <= 0 ||
            R_TechPath(1, product, (techstep_t[TECH_PATH_MAX]){0}, TECH_PATH_MAX) != 0) continue;
        if (!wanted) wanted = product;
        else if (!G_CountPlannedActors(1, G_ModelActorIdForProduct(product))) provider = product;
    }
    CHECK(wanted && provider);
    provider_ui = provider->ui_id;
    maker = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && !maker; th = th->next)
        for (int m = 0; m < wanted->maker_count; ++m)
            if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == wanted->makers[m] &&
                ((mobj_t *)th)->owner == 1) maker = (mobj_t *)th;
    CHECK(maker);

    static const productreq_t rows[] = { {0, {REQ_UPGRADE, 5, 2}}, {0, {REQ_TECH, 0, 2}} };
    productreq_t mine[2];
    memcpy(mine, rows, sizeof(mine));
    mine[0].product = mine[1].product = wanted->ui_id;
    ruleset_t rules = g_ruleset;
    rules.requirements = mine;
    rules.requirement_count = 2;
    rules.upgrade_level = fake_upgrade_level;
    rules.upgrade_product = fake_upgrade_product;
    rules.tech_level = fake_tech_level;
    rules.research = fake_research;
    R_RulesOverride(&rules);

    techstep_t steps[TECH_PATH_MAX];
    CHECK(R_ProducerLackingTech(1, wanted) == maker);
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) == 2);
    CHECK(steps[0].req.kind == REQ_UPGRADE && steps[0].req.level == 2 && steps[0].product == provider->ui_id &&
          steps[0].wanted_by == wanted->ui_id && !steps[0].pending);
    CHECK(steps[1].req.kind == REQ_TECH && steps[1].req.level == 2 && steps[1].product == 0 &&
          steps[1].wanted_by == wanted->ui_id);
    techstep_t next;
    CHECK(R_TechNextStep(1, wanted, &next) && next.req.kind == REQ_UPGRADE);

    /* Once the upgrade stands, only the tech is left; once raised, nothing. */
    fake_level = 2;
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) == 1 && steps[0].req.kind == REQ_TECH);
    CHECK(R_Rules()->research(&level, maker) && fake_tech == 1);
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) == 1);
    CHECK(R_Rules()->research(&level, maker));
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) == 0 && !R_TechNextStep(1, wanted, &next));
    CHECK(R_RowsMet(1, wanted));

    /* No product raises the upgrade: the target cannot be reached by buying. */
    fake_level = 0;
    provider_ui = -12345;
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) < 0);
    /* A product that needs itself, through its upgrade, is no path either. */
    provider_ui = wanted->ui_id;
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) < 0);
    R_RulesOverride(NULL);
    CHECK(R_TechPath(1, wanted, steps, TECH_PATH_MAX) == 0);

    P_FreeLevel(&level);
    rts_game_model_destroy(model);
    puts("PASS: tech paths through synthetic upgrade and tech rows, unreachable and cyclic targets");
    return 0;
}
