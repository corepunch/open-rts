#include "../rts_model_test.h"
#include "game.h"
#include "info.h"
#include "dr_types.h"
#include "p_local.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define CHECK(c) RTS_CHECK(c, "Dark Reign terrain classes", #c)

static int class_by_name(const char *name) {
    static const char *names[] = {"", "Wheel", "Wheelf", "Wheela", "Track", "Foot", "Hover",
                                  "Hovers", "Flying", "LeggedDroid"};
    for (int i = 1; i < DR_MOVE_COUNT; ++i)
        if (!strcasecmp(names[i], name)) return i;
    return 0;
}

/* Every actor type's class must be one its sprite's UNITS.TXT definitions declare
 * (a few sprites are shared between definitions with different UseEffects). */
static int audit_unit_classes(void) {
    FILE *file = fopen("data/REIGN/dark/deftxt/UNITS.TXT", "r");
    CHECK(file);
    static struct { char sprite[64]; int move_class; } defs[256];
    int count = 0;
    char line[512], sprite[64] = "";
    int block_class = -1;
    while (fgets(line, sizeof(line), file) && count < 256) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        if (strstr(line, "DefineUnitType(")) { sprite[0] = '\0'; block_class = -1; continue; }
        char *call = strstr(line, "SetImage(");
        if (call) sscanf(call, "SetImage(%63[^) \t]", sprite);
        call = strstr(line, "UseEffects(");
        char effect[32];
        if (call && sscanf(call, "UseEffects(%31[^)])", effect) == 1) block_class = class_by_name(effect);
        if (sprite[0] && block_class >= 0) { /* Either order within a block. */
            snprintf(defs[count].sprite, sizeof(defs[count].sprite), "%s", sprite);
            defs[count++].move_class = block_class;
            sprite[0] = '\0';
            block_class = -1;
        }
    }
    fclose(file);
    int audited = 0;
    for (int i = 0; i < num_actor_types; ++i) {
        if (!actor_types[i].sprite_name) continue;
        bool known = false, match = false;
        for (int d = 0; d < count; ++d) {
            if (strcasecmp(defs[d].sprite, actor_types[i].sprite_name)) continue;
            known = true;
            match |= defs[d].move_class == actor_types[i].move_class;
        }
        if (!known) continue; /* Buildings carry no UseEffects. */
        if (!match) {
            fprintf(stderr, "%s (%s): class %d is declared by no UNITS.TXT definition\n",
                    actor_types[i].name, actor_types[i].sprite_name, actor_types[i].move_class);
            return 1;
        }
        ++audited;
    }
    CHECK(audited > 40);
    return 0;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN",
    };
    CHECK(model && rts_game_model_load(model, &config));
    CHECK(level.speeds && level.cell_terrain && level.cell_effect && level.cell_solid);

    /* Values straight from TRNEFF.TXT. */
    const terrainspeeds_t *speeds = level.speeds;
    CHECK(speeds->terrain[DR_MOVE_FOOT][0] == 0 && speeds->terrain[DR_MOVE_HOVER][0] == 100);
    CHECK(speeds->terrain[DR_MOVE_FOOT][4] == 50 && speeds->terrain[DR_MOVE_TRACK][6] == 25);
    CHECK(speeds->terrain[DR_MOVE_WHEEL][8] == 200 && speeds->terrain[DR_MOVE_TRACK][8] == 150);
    CHECK(speeds->overlay[DR_MOVE_FOOT][3] == 0 && speeds->overlay[DR_MOVE_FOOT][5] == 25);
    CHECK(speeds->overlay[DR_MOVE_HOVER][6] == 20 && speeds->overlay[DR_MOVE_FLYING][3] == 100);
    CHECK(speeds->max_slope[DR_MOVE_FOOT] == 3 && speeds->max_slope[DR_MOVE_TRACK] == 2);
    RTS_RUN(audit_unit_classes());

    /* Liquid stops feet and wheels but carries hovercraft; nothing crosses solid rock. */
    int liquid = 0, slow = 0;
    for (int y = 0; y < level.height; ++y)
        for (int x = 0; x < level.width; ++x) {
            int i = L_Index(&level, x, y);
            if (level.cell_terrain[i] == 0 && !level.cell_solid[i] && level.cell_effect[i] == 255) {
                CHECK(L_MoveSpeed(&level, DR_MOVE_FOOT, x, y) == 0);
                CHECK(L_MoveSpeed(&level, DR_MOVE_HOVER, x, y) == 100);
                ++liquid;
            }
            if (level.cell_solid[i]) {
                for (int c = 1; c < DR_MOVE_COUNT; ++c) CHECK(L_MoveSpeed(&level, c, x, y) == 0);
            }
            slow += L_MoveSpeed(&level, DR_MOVE_FOOT, x, y) == 50;
        }
    printf("M01F: %d liquid cells, %d half-speed cells for infantry\n", liquid, slow);

    /* A tracked tank and a hover tank cross the same map with their own speed tables. */
    const mobjtype_t *track = NULL, *hover = NULL;
    for (int i = 0; i < num_actor_types; ++i) {
        if (actor_types[i].move_class == DR_MOVE_TRACK && !track) track = &actor_types[i];
        if (actor_types[i].move_class == DR_MOVE_HOVER && (actor_types[i].traits & MF_MOBILE) &&
            !(actor_types[i].traits & MF_FLY) && !hover) hover = &actor_types[i];
    }
    CHECK(track && hover);
    mobj_t *a = P_SpawnMobj(fixed3_from_fvec2(fvec2_cell_center((ivec2_t){8, 8}), 0), track->id);
    CHECK(a && P_MobjMoveClass(a) == DR_MOVE_TRACK);
    mobj_t *b = P_SpawnMobj(fixed3_from_fvec2(fvec2_cell_center((ivec2_t){9, 8}), 0), hover->id);
    CHECK(b && P_MobjMoveClass(b) == DR_MOVE_HOVER);
    rts_game_model_destroy(model);
    puts("PASS: Dark Reign terrain speed classes load from TRNEFF.TXT and match UNITS.TXT");
    return 0;
}
