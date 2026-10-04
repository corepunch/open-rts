#include "t_local.h"
#include "info.h"
#include "w2_local.h"

#include <ctype.h>
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II catalog", #c)

/* Optional, independent audit against the pinned reference, not a second
 * runtime balance table. Ordinary checks still run without reference/. */
static char *read_reference(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char *data = calloc((size_t)length + 1, 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); data = NULL;
    }
    fclose(file);
    return data;
}

static void remove_comments(char *text) {
    bool quote = false;
    for (char *p = text; *p; ++p) {
        if (quote && *p == '\\' && p[1]) { ++p; continue; }
        if (*p == '"') quote = !quote;
        if (!quote && p[0] == '-' && p[1] == '-') {
            while (*p && *p != '\n') *p++ = ' ';
            if (!*p) break;
        }
    }
}

static char *closing_brace(char *text) {
    int depth = 0;
    bool quote = false;
    for (char *p = text; *p; ++p) {
        if (quote && *p == '\\' && p[1]) { ++p; continue; }
        if (*p == '"') quote = !quote;
        if (!quote && *p == '{') ++depth;
        if (!quote && *p == '}' && --depth == 0) return p;
    }
    return NULL;
}

static char *field(char *text, const char *name) {
    size_t length = strlen(name);
    for (char *p = text; (p = strstr(p, name)); p += length) {
        if (p != text && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
        char *value = p + length;
        while (isspace((unsigned char)*value)) ++value;
        if (*value++ != '=') continue;
        while (isspace((unsigned char)*value)) ++value;
        return value;
    }
    return NULL;
}

static int number(char *text, const char *name) {
    char *value = field(text, name);
    if (!value) return 0;
    if (*value == '{') value = field(value, "Value");
    return value ? atoi(value) : 0;
}

static bool boolean(char *text, const char *name) {
    char *value = field(text, name);
    return value && (!strncmp(value, "true", 4) || atoi(value) != 0);
}

static bool numeric(const mobjinfo_t *unit, const char *name, int actual, int expected) {
    if (actual == expected) return true;
    fprintf(stderr, "%s: %s imported=%d reference=%d\n", unit->name, name, actual, expected);
    return false;
}

static bool scalar(const mobjinfo_t *unit, char *text, const char *name, int actual) {
    return numeric(unit, name, actual, number(text, name));
}

static int resource(char *text, const char *name) {
    if (!text) return 0;
    char token[64];
    snprintf(token, sizeof(token), "\"%s\"", name);
    char *p = strstr(text, token);
    if (!p) return 0;
    p += strlen(token);
    while (isspace((unsigned char)*p) || *p == ',') ++p;
    return atoi(p);
}

static int audit_unit(mobjinfo_t *unit, char *text) {
    const w2_stats_t *s = &unit->w2;
    CHECK(scalar(unit, text, "HitPoints", unit->spawnhealth));
    CHECK(scalar(unit, text, "Speed", s->speed));
    CHECK(numeric(unit, "Armor", s->armor, unit->doomednum == MT_BALLISTA ? 0 : number(text, "Armor")));
    CHECK(numeric(unit, "BasicDamage", s->basic_damage, unit->doomednum == MT_CRITTER ? 0 : number(text, "BasicDamage")));
    CHECK(scalar(unit, text, "PiercingDamage", s->piercing_damage));
    CHECK(scalar(unit, text, "SightRange", s->sight));
    CHECK(scalar(unit, text, "MaxAttackRange", s->attack_range));
    CHECK(scalar(unit, text, "MinAttackRange", s->min_attack_range));
    CHECK(scalar(unit, text, "ComputerReactionRange", s->reaction_range.computer));
    CHECK(scalar(unit, text, "PersonReactionRange", s->reaction_range.person));
    CHECK(scalar(unit, text, "Supply", s->food.supply));
    CHECK(numeric(unit, "Demand", s->food.demand, unit->doomednum == MT_CRITTER ? 0 : number(text, "Demand")));
    CHECK(scalar(unit, text, "RepairHp", s->repair.hp));
    CHECK(scalar(unit, text, "RepairRange", s->repair.range));
    CHECK(scalar(unit, text, "AutoRepairRange", s->repair.auto_range));
    CHECK(scalar(unit, text, "Points", s->points));
    CHECK(scalar(unit, text, "Priority", s->priority));
    CHECK(scalar(unit, text, "AnnoyComputerFactor", s->annoyance));
    CHECK(scalar(unit, text, "DecayRate", s->decay));
    CHECK(scalar(unit, text, "MaxOnBoard", s->transport_capacity));
    CHECK(!!(s->flags & W2_AIR) == boolean(text, "AirUnit"));
    CHECK(!!(s->flags & W2_SEA) == boolean(text, "SeaUnit"));
    CHECK(numeric(unit, "Building", !!(s->flags & W2_STRUCTURE), boolean(text, "Building")));
    char *domain = field(text, "Type");
    CHECK(domain);
    CHECK(s->domain == (!strncmp(domain, "\"fly\"", 5) ? W2_DOMAIN_AIR :
                        !strncmp(domain, "\"naval\"", 7) ? W2_DOMAIN_SEA : W2_DOMAIN_LAND));
    char *given = field(text, "GivesResource");
    CHECK(s->gives_mask == (given ? (!strncmp(given, "\"gold\"", 6) ? 1 :
                                  !strncmp(given, "\"wood\"", 6) ? 2 : 4) : 0));
    char *projectile = field(text, "Missile");
    CHECK(projectile && *projectile == '"' && s->projectile);
    char *projectile_end = strchr(projectile + 1, '"');
    CHECK(projectile_end && strlen(s->projectile) == (size_t)(projectile_end - projectile - 1));
    CHECK(!strncmp(s->projectile, projectile + 1, (size_t)(projectile_end - projectile - 1)));
    CHECK(s->target_mask == (boolean(text, "CanTargetLand") ? W2_TARGET_LAND : 0) +
                            (boolean(text, "CanTargetSea") ? W2_TARGET_SEA : 0) +
                            (boolean(text, "CanTargetAir") ? W2_TARGET_AIR : 0));
    const struct { const char *key; unsigned bit; } attributes[] = {
        {"organic", W2_ORGANIC}, {"isundead", W2_UNDEAD}, {"hero", W2_HERO},
        {"volatile", W2_VOLATILE}, {"DetectCloak", W2_DETECT_CLOAK},
        {"PermanentCloak", W2_PERMANENT_CLOAK}, {"Indestructible", W2_INDESTRUCTIBLE},
        {"Coward", W2_COWARD}, {"GroundAttack", W2_GROUND_ATTACK},
        {"SelectableByRectangle", W2_RECT_SELECT}, {"VisibleUnderFog", W2_VISIBLE_UNDER_FOG},
        {"ShoreBuilding", W2_SHORE_BUILDING}, {"BuilderOutside", W2_BUILDER_OUTSIDE},
        {"Elevated", W2_ELEVATED}, {"SideAttack", W2_SIDE_ATTACK}, {"CanAttack", W2_CAN_ATTACK},
        {"Neutral", W2_NEUTRAL}, {"Teleporter", W2_TELEPORTER}, {"CanHarvest", W2_CAN_HARVEST},
    };
    for (size_t i = 0; i < sizeof(attributes) / sizeof(*attributes); ++i)
        CHECK(numeric(unit, attributes[i].key, !!(s->attributes & attributes[i].bit), boolean(text, attributes[i].key)));
    CHECK(s->mana.max == (field(text, "Mana") ? 255 : 0));
    CHECK(s->mana.initial == (s->mana.max ? 85 : 0));
    CHECK(s->mana.increase == (s->mana.max ? 1 : 0));
    CHECK(numeric(unit, "Level", s->level, field(text, "Level") ? number(text, "Level") : 1));
    const struct { const char *key; isize2_t value; } sizes[] = {
        {"TileSize", s->footprint}, {"BoxSize", s->box},
    };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
        char *p = field(text, sizes[i].key);
        isize2_t expected = {0};
        CHECK(p && sscanf(p, "{ %d, %d", &expected.w, &expected.h) == 2);
        CHECK(!memcmp(&sizes[i].value, &expected, sizeof(expected)));
    }
    const char *keys[] = {"Costs", "RepairCosts", "ImproveProduction", "CanStore"};
    const char *resources[] = {"gold", "wood", "oil"};
    for (int i = 0; i < 4; ++i) {
        char *p = field(text, keys[i]);
        if (!p) {
            CHECK(i == 0 ? !s->costs.time : true);
            for (int r = 0; r < 3; ++r)
                CHECK((i == 0 ? s->costs.resources[r] : i == 1 ? s->repair.costs[r] :
                      i == 2 ? s->income[r] : s->store_mask & (1 << r)) == 0);
            continue;
        }
        char *end = closing_brace(p);
        CHECK(end);
        char saved = *end;
        *end = 0;
        if (i == 0) CHECK(s->costs.time == resource(p, "time"));
        for (int r = 0; r < 3; ++r) {
            int actual = i == 0 ? s->costs.resources[r] : i == 1 ? s->repair.costs[r] :
                         i == 2 ? s->income[r] : !!(s->store_mask & (1 << r));
            char token[16]; snprintf(token, sizeof(token), "\"%s\"", resources[r]);
            CHECK(numeric(unit, keys[i], actual, i == 3 ? !!strstr(p, token) : resource(p, resources[r])));
        }
        *end = saved;
    }
    char *gather = field(text, "CanGatherResources");
    unsigned seen = 0;
    if (gather) {
        char *outer_end = closing_brace(gather);
        CHECK(outer_end);
        char saved_outer = *outer_end;
        *outer_end = 0;
        for (char *p = gather + 1; (p = strchr(p, '{')); ) {
            char *end = closing_brace(p);
            CHECK(end);
            char saved = *end; *end = 0;
            int r = strstr(p, "\"gold\"") ? 0 : strstr(p, "\"wood\"") ? 1 : 2;
            const w2_gather_t *g = &s->gather[r];
            CHECK(g->capacity == resource(p, "resource-capacity"));
            CHECK(g->step == resource(p, "resource-step"));
            CHECK(g->resource_wait == resource(p, "wait-at-resource"));
            CHECK(g->depot_wait == resource(p, "wait-at-depot"));
            CHECK(g->terrain == !!strstr(p, "\"terrain-harvester\""));
            CHECK(g->outside == !!strstr(p, "\"harvest-from-outside\""));
            CHECK(g->lose_loaded == !!strstr(p, "\"lose-resources\""));
            CHECK(g->refinery == !!strstr(p, "\"refinery-harvester\""));
            seen |= 1u << r;
            *end = saved; p = end + 1;
        }
        *outer_end = saved_outer;
    }
    for (int r = 0; r < 3; ++r)
        if (!(seen & (1u << r))) CHECK(s->gather[r].capacity == 0);
    char *spells = field(text, "CanCastSpell");
    int count = 0;
    if (spells) {
        char *end = closing_brace(spells);
        CHECK(end);
        for (char *p = spells; (p = strchr(p, '"')) && p < end; ) {
            char *stop = strchr(++p, '"');
            CHECK(stop && stop < end && count < 6 && s->spells[count]);
            CHECK(strlen(s->spells[count]) == (size_t)(stop - p));
            CHECK(!strncmp(p, s->spells[count], (size_t)(stop - p)));
            ++count; p = stop + 1;
        }
    }
    if (count < 6) CHECK(!s->spells[count]);
    return 0;
}

static int test_reference(void) {
    char *mapping = read_reference("reference/wargus/pud.cpp");
    if (!mapping) { puts("SKIP: optional pinned Wargus catalog audit (reference/wargus absent)"); return 0; }
    const char *paths[] = {"reference/wargus/scripts/human/units.lua",
                           "reference/wargus/scripts/orc/units.lua", "reference/wargus/scripts/units.lua"};
    char *sources[3];
    for (int i = 0; i < 3; ++i) { sources[i] = read_reference(paths[i]); CHECK(sources[i]); remove_comments(sources[i]); }
    char *p = strstr(mapping, "const char *UnitScriptNames[]");
    CHECK(p && (p = strchr(p, '{')));
    int audited = 0;
    for (int type = 1; type < NUMMOBJTYPES; ++type) {
        CHECK((p = strchr(p, '"')));
        char *end = strchr(++p, '"');
        CHECK(end && end - p < 128);
        char name[128] = {0}; memcpy(name, p, (size_t)(end - p)); p = end + 1;
        if (!name[0]) { CHECK(!mobjinfo[type].name); continue; }
        char pattern[160]; snprintf(pattern, sizeof(pattern), "DefineUnitType(\"%s\"", name);
        char *unit = NULL;
        for (int i = 0; i < 3 && !unit; ++i) unit = strstr(sources[i], pattern);
        CHECK(unit && (unit = strchr(unit, '{')));
        char *last = closing_brace(unit);
        CHECK(last);
        char saved = last[1]; last[1] = 0;
        RTS_RUN(audit_unit(&mobjinfo[type], unit));
        last[1] = saved; ++audited;
    }
    CHECK(audited == 100);
    free(mapping);
    for (int i = 0; i < 3; ++i) free(sources[i]);
    puts("PASS: all 100 defined PUD types match the pinned Wargus base stats, with documented overrides");
    return 0;
}

static int test_roster(void) {
    G_InitGame();
    P_InitThinkers();
    CHECK(NUMMOBJTYPES == 106 && gameinfo->mobj_type_count == NUMMOBJTYPES);
    int defined = 0, reserved = 0, spawnable = 0;
    for (int type = 1; type < NUMMOBJTYPES; ++type) {
        const mobjinfo_t *info = &mobjinfo[type];
        CHECK(info->doomednum == type);
        if (!info->name) { CHECK(info->w2.flags == W2_SKIP); ++reserved; continue; }
        ++defined;
        CHECK(info->label && w2_pud_named(info->name) == type - 1);
        CHECK(info->spawnhealth == actor_types[type - 1].max_hp ||
              (info->spawnhealth == 0 && actor_types[type - 1].max_hp == 1));
        CHECK(info->damage == info->w2.basic_damage + info->w2.piercing_damage);
        CHECK(info->w2.damage_min <= info->damage);
        CHECK(info->w2.sight >= 0 && info->w2.attack_range >= info->w2.min_attack_range);
        CHECK(info->spawnstate > 0 && info->spawnstate < W2_STATE_COUNT);
        if (info->w2.flags & W2_SKIP) continue;
        mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){8, 8}, 0), (uint16_t)type);
        CHECK(unit && unit->type_id == type && unit->hp == actor_types[type - 1].max_hp);
        CHECK(!strcmp(unit->core.sprite_name, info->name));
        ++spawnable;
    }
    CHECK(defined == 100 && reserved == 5 && spawnable == 96);
    P_FreeLevel(&level);
    G_InitGame(); /* Reinitializing state tables must preserve authored stats. */
    CHECK(mobjinfo[MT_ALLERIA].spawnhealth == 120 && mobjinfo[MT_DEATHWING].spawnhealth == 800);
    CHECK(mobjinfo[MT_MAGE].w2.mana.max == 255 && mobjinfo[MT_MAGE].w2.mana.initial == 85);
    CHECK(mobjinfo[MT_HUMAN_TRANSPORT].w2.transport_capacity == 6);
    const StaticProductDefinition *footman = G_ModelProductByUIId(NULL, 1);
    CHECK(footman && footman->cost == 600 && G_ModelProductTrainingTimeMs(footman) == 60000);
    CHECK(mobjinfo[MT_FOOTMAN].w2.basic_damage == 6 && mobjinfo[MT_FOOTMAN].w2.piercing_damage == 3);
    puts("PASS: 105 PUD slots, 100 named definitions, five reserved slots, 96 spawnable types and stat-backed production");
    return 0;
}

int main(void) {
    RTS_RUN(test_roster());
    RTS_RUN(test_reference());
    return 0;
}
