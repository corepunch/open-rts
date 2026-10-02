/* Print retail Dark Reign unit definitions from deftxt/UNITS.TXT.

   One row per DefineUnitType block: native identifier, side, cost and build
   time, move mode, movement effects, strength, SetPhysics(mass speed), hit
   size, seeing range, first part image and description.

   Usage:
     dr_units_extract <dark-reign-root>
*/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char ident[48], description[48], move_mode[16], effects[16], image[32];
    int side, cost, build_time, strength, mass, speed, hit_size, sight;
} dr_unit_t;

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (!text || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

static void print_unit(const dr_unit_t *u) {
    printf("%-28s %4d %5d %4d %-6s %-6s %5d %4d %5d %3d %5d %-14s %s\n",
           u->ident, u->side, u->cost, u->build_time, u->move_mode, u->effects,
           u->strength, u->mass, u->speed, u->hit_size, u->sight, u->image,
           u->description);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: dr_units_extract <dark-reign-root>\n");
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/deftxt/UNITS.TXT", argv[1]);
    char *text = read_text(path);
    if (!text) {
        fprintf(stderr, "dr_units_extract: cannot read %s: %s\n", path, strerror(errno));
        return 1;
    }

    printf("%-28s %4s %5s %4s %-6s %-6s %5s %4s %5s %3s %5s %-14s %s\n",
           "ident", "side", "cost", "time", "move", "effect", "hp", "mass",
           "speed", "hit", "sight", "image", "description");

    dr_unit_t unit = { 0 };
    int depth = 0, count = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        while (isspace((unsigned char)*line)) ++line;

        char ident[sizeof(unit.ident)];
        if (depth == 0) {
            if (sscanf(line, "DefineUnitType(%47[^)])", ident) == 1) {
                unit = (dr_unit_t){ 0 };
                memcpy(unit.ident, ident, sizeof(ident));
            }
        } else if (depth == 1) {
            sscanf(line, "SetDescription(%47[^)])", unit.description);
            sscanf(line, "SetSide(%d", &unit.side);
            sscanf(line, "SetCost(%d %d", &unit.cost, &unit.build_time);
            sscanf(line, "UseEffects(%15[^)])", unit.effects);
            sscanf(line, "SetMoveMode(%15[^)])", unit.move_mode);
            sscanf(line, "SetStrength(%d", &unit.strength);
            sscanf(line, "SetPhysics(%d %d", &unit.mass, &unit.speed);
            sscanf(line, "SetHitSize(%d", &unit.hit_size);
            sscanf(line, "SetSeeingRange(%d", &unit.sight);
        } else if (depth == 2 && unit.image[0] == '\0') {
            sscanf(line, "SetImage(%31[^)])", unit.image);
        }

        for (const char *p = line; *p; ++p) {
            if (*p == '{') {
                ++depth;
            } else if (*p == '}' && depth > 0 && --depth == 0 && unit.ident[0]) {
                print_unit(&unit);
                unit.ident[0] = '\0';
                ++count;
            }
        }
    }
    free(text);
    fprintf(stderr, "dr_units_extract: %d unit types\n", count);
    return count > 0 ? 0 : 1;
}
