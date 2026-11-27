#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront catalog", #c)

/* Rustfront's catalog: every unit and structure of every faction has an actor type, a state
 * table row and a sprite; every product a cost, a build time, makers and prerequisites of its own
 * faction. OPEN_RTS_RF_CATALOG=FILE also writes the catalog as JSON for the field manual
 * (docs/_data/rustfront.json). */

static const char *const FACTION_KEY[RF_FACTION_COUNT] = {"directorate", "clans", "concord"};

/* The unit's name on disk: its sprite sheet without the layer, "tank" from "tank_hull". */
static void key_of(const mobjtype_t *type, char *out, size_t size) {
    snprintf(out, size, "%s", type->sprite_name ? type->sprite_name : "");
    char *layer = strrchr(out, '_');
    if (layer && (!strcmp(layer, "_body") || !strcmp(layer, "_hull") || !strcmp(layer, "_base"))) *layer = '\0';
}

static void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; ++s) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static const char *label_of(int type) {
    const mobjtype_t *t = P_ActorType((uint16_t)type);
    return t ? t->name : "?";
}

int main(void) {
    FILE *json = NULL;
    const char *path = getenv("OPEN_RTS_RF_CATALOG");
    if (path) CHECK((json = fopen(path, "w")) != NULL);
    if (json) fputs("{\"units\": [\n", json);
    int written = 0, products = 0;
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        for (int id = rf_factions[f].first; id <= rf_factions[f].last; ++id) {
            const mobjtype_t *type = P_ActorType((uint16_t)id);
            CHECK(type && type->name && type->sprite_name);
            CHECK(RF_FactionOfType((uint16_t)id) == f);
            CHECK(mobjinfo[id].spawnstate > S_NULL && mobjinfo[id].spawnstate < NUMSTATES);
            const StaticProductDefinition *p = G_ModelProductByUIId(NULL, id);
            if (p) {
                ++products;
                CHECK(p->cost > 0 && p->label && G_ModelProductTrainingTimeMs(p) > 0 && p->icon_frame >= 0);
                CHECK(p->maker_count >= 1);
                for (int m = 0; m < p->maker_count; ++m) CHECK(RF_FactionOfType((uint16_t)p->makers[m]) == f);
                for (int m = 0; m < p->prerequisite_count; ++m)
                    CHECK(RF_FactionOfType((uint16_t)p->prerequisites[m]) == f);
                CHECK((p->product_class == RTS_PRODUCT_BUILDING) == (type->footprint.w > 0));
            }
            if (!json) continue;
            char key[64];
            key_of(type, key, sizeof(key));
            bool structure = type->footprint.w > 0 && !(type->traits & MF_MOBILE);
            fprintf(json, "%s  {\"key\": ", written++ ? ",\n" : "");
            json_string(json, key);
            fprintf(json, ", \"id\": %d, \"faction\": \"%s\", \"name\": ", id, FACTION_KEY[f]);
            json_string(json, type->name);
            fprintf(json, ", \"kind\": \"%s\", \"hp\": %d", structure ? "structure" : RF_IsInfantry((uint16_t)id) ? "infantry"
                          : (type->traits & MF_FLY) ? "aircraft" : (type->traits & MF_MOBILE) ? "vehicle" : "other",
                    type->max_hp);
            if (type->traits & MF_MOBILE) fprintf(json, ", \"speed\": %.2f", type->speed / 65536.0);
            if (type->traits & MF_ATTACK) {
                fprintf(json, ", \"damage\": %d, \"range\": %.2f, \"reload\": %.3f, \"anti_air\": %s",
                        type->attack.damage, type->attack.range / 65536.0, type->attack.cooldown_ms / 1000.0,
                        RF_AntiAir((uint16_t)id) ? "true" : "false");
                if (type->attack.min_range)
                    fprintf(json, ", \"min_range\": %.2f", type->attack.min_range / 65536.0);
            }
            if (type->footprint.w > 0) fprintf(json, ", \"footprint\": [%d, %d]", type->footprint.w, type->footprint.h);
            fprintf(json, ", \"sight\": %d, \"power\": %d, \"detector\": %s", RF_SightCells((uint16_t)id),
                    RF_PowerOf((uint16_t)id), (type->traits & MF_DETECTOR) ? "true" : "false");
            if (p) {
                fprintf(json, ", \"cost\": %d, \"build_s\": %.1f, \"label\": ", p->cost,
                        G_ModelProductTrainingTimeMs(p) / 1000.0);
                json_string(json, p->label);
                fputs(", \"made_at\": [", json);
                for (int m = 0; m < p->maker_count; ++m) {
                    if (m) fputs(", ", json);
                    json_string(json, label_of(p->makers[m]));
                }
                fputs("], \"needs\": [", json);
                for (int m = 0; m < p->prerequisite_count; ++m) {
                    if (m) fputs(", ", json);
                    json_string(json, label_of(p->prerequisites[m]));
                }
                fputs("]", json);
            }
            fputs("}", json);
        }
    if (json) {
        fputs("\n]}\n", json);
        fclose(json);
    }
    CHECK(products >= 50);
    printf("PASS: %d products across three factions, each with its makers, needs, cost and build time\n", products);
    return 0;
}
