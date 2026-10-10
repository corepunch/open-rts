/* Every actor's stats must match its retail UNITS.CFG row. */
#include "engine.h"
#include "kknd.h"
#include "info.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* "-" is an absent native value. */
static int column(char **cols, int count, int i) {
    return i < count && strcmp(cols[i], "-") != 0 ? atoi(cols[i]) : 0;
}

int main(void) {
    FILE *f = fopen("data/KKND/UNITS.CFG", "r");
    assert(f);
    char line[512];
    int checked = 0;
    while (fgets(line, sizeof(line), f)) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        char *cols[16];
        int n = 0;
        for (char *tok = strtok(line, " \t\r\n"); tok && n < 16; tok = strtok(NULL, " \t\r\n"))
            cols[n++] = tok;
        if (n < 4 || strncmp(cols[0], "UNIT_", 5) != 0) continue;
        const mobjtype_t *type = NULL;
        for (int i = 0; i < num_actor_types && !type; ++i)
            if (cplc_names[actor_types[i].id] &&
                strcasecmp(cplc_names[actor_types[i].id], cols[0]) == 0) type = &actor_types[i];
        assert(type);
        int hp = column(cols, n, 3), speed = column(cols, n, 4);
        int reload = column(cols, n, 5), reload2 = column(cols, n, 6), volley = column(cols, n, 7);
        int tspeed = column(cols, n, 8), range = column(cols, n, 9);
        int dmg[3] = { column(cols, n, 11), column(cols, n, 12), column(cols, n, 13) };
        if (type->max_hp != hp || type->speed != speed * (FIXED_ONE / 32) ||
            type->turn_step != ANG90 / RTS_TICRATE * (angle_t)tspeed) {
            fprintf(stderr, "%s: hp %d speed %d turn %u\n", cols[0],
                    type->max_hp, type->speed, type->turn_step);
            return 1;
        }
        if (type->traits & MF_ATTACK) {
            assert(type->attack.versus[0] == dmg[0] && type->attack.versus[1] == dmg[1] &&
                   type->attack.versus[2] == dmg[2] && type->attack.damage == dmg[0]);
            /* Bombers carry no native range or reload. */
            if (range) {
                assert(type->attack.range == range * (FIXED_ONE / 32));
                assert(type->attack.cooldown_ms == reload * 1000 / 60);
                assert(type->attack.shots == volley);
                assert(type->attack.reload_ms == reload2 * 1000 / 60);
            }
        }
        unsigned armor = !speed ? 2u : tspeed == 64 ? 0u : 1u;
        assert(type->armor_class == armor);
        ++checked;
    }
    fclose(f);
    assert(checked == num_actor_types);
    printf("PASS: %d KKnD actors match UNITS.CFG\n", checked);
    return 0;
}
