/* Generate Dark Reign's Doom-style state and mobjinfo tables.
   The catalog maps retail DEFTXT actors to the body sprites also used by OpenDR. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "info_gen.h"

#define MOBILE(type, sprite, actor, hp, speed, damage, extra) \
    { type, sprite, sprite ".spr", actor, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra }
#define BUILDING(type, sprite, actor, hp, damage, extra) \
    { type, sprite, sprite ".spr", actor, hp, 0, 0, 0, 0, damage, \
      "MF_SELECTABLE|MF_RENDERABLE" extra }

static const info_entry_t entries[] = {
    MOBILE("FG_CONSTRUCTION_CREW", "UCFCNST0", "ACTOR_FG_CONSTRUCTION_CREW", 100, 6, 20, "|MF_ATTACK"),
    MOBILE("FG_FREIGHTER", "UCFRGST0", "ACTOR_FG_GROUND_TRANSPORTER", 750, 5, 0, "|MF_HARVESTER"),
    MOBILE("FG_HOVER_FREIGHTER", "UCHFRST0", "ACTOR_FG_HOVER_TRANSPORTER", 500, 5, 11, "|MF_HARVESTER|MF_ATTACK"),
    MOBILE("FG_RAIDER", "UFRADST0", "ACTOR_FG_RAIDER", 100, 5, 11, "|MF_ATTACK"),
    MOBILE("FG_MERCENARY", "UFMRCST0", "ACTOR_FG_MERCENARY", 125, 5, 11, "|MF_ATTACK"),
    MOBILE("FG_SNIPER", "UFSNPST0", "ACTOR_FG_SNIPER", 100, 5, 150, "|MF_ATTACK"),
    MOBILE("FG_SCOUT", "UFSCTST0", "ACTOR_FG_SCOUT", 66, 6, 0, ""),
    MOBILE("FG_MEDIC", "UFMEDST0", "ACTOR_FG_MEDIC", 66, 5, 0, ""),
    MOBILE("FG_SABOTEUR", "UFSABST0", "ACTOR_FG_SABOTEUR", 100, 5, 0, ""),
    MOBILE("FG_MECHANIC", "UFMECST0", "ACTOR_FG_MECHANIC", 66, 5, 0, ""),
    MOBILE("FG_MARTYR", "UFMTRST0", "ACTOR_FG_SUICIDE_NUKER", 100, 5, 180, "|MF_ATTACK"),
    MOBILE("FG_SPY", "UCINFST0", "ACTOR_FG_SPY", 66, 5, 0, ""),
    MOBILE("FG_SPYDER_BIKE", "UFSPBST0", "ACTOR_FG_SPYDER_BIKE", 133, 7, 10, "|MF_ATTACK"),
    MOBILE("FG_IFV", "UFRATST0", "ACTOR_FG_IFV", 200, 5, 0, ""),
    MOBILE("FG_MEDIUM_TANK", "UFSKTST0", "ACTOR_FG_MEDIUM_TANK", 133, 4, 14, "|MF_ATTACK"),
    MOBILE("FG_TANK_HUNTER", "UFTHNST0", "ACTOR_FG_TANK_HUNTER", 150, 4, 60, "|MF_ATTACK"),
    MOBILE("FG_PHASE_TANK", "UFPHTST0", "ACTOR_FG_PHASE_TANK", 166, 4, 30, "|MF_ATTACK"),
    MOBILE("FG_MAD", "UFFLKST0", "ACTOR_FG_MAD", 100, 4, 8, "|MF_ATTACK"),
    MOBILE("FG_TRIPLE_RAIL_TANK", "UFTRTST0", "ACTOR_FG_TRIPLE_RAIL_TANK", 200, 4, 24, "|MF_ATTACK"),
    MOBILE("FG_SPA", "UFFARST0", "ACTOR_FG_SPA", 133, 4, 30, "|MF_ATTACK"),
    MOBILE("FG_SKY_BIKE", "UFSKBST0", "ACTOR_FG_SKY_BIKE", 100, 6, 10, "|MF_ATTACK|MF_FLY"),
    MOBILE("FG_OUTRIDER", "UFOUTST0", "ACTOR_FG_OUTRIDER", 200, 5, 20, "|MF_ATTACK|MF_FLY"),
    MOBILE("FG_SHOCKWAVE", "UFSWVST0", "ACTOR_FG_SHOCKWAVE", 166, 4, 17, "|MF_ATTACK"),
    MOBILE("FG_CONTAMINATOR", "UCWCOST0", "ACTOR_FG_CONTAMINATOR", 166, 3, 5, "|MF_ATTACK"),
    MOBILE("FG_UNDERGROUND_TUNNEL", "UFPHRST0", "ACTOR_FG_UNDERGROUND_TUNNEL", 150, 5, 0, ""),
    MOBILE("FG_BASE_MOVER", "UFBAMST0", "ACTOR_FG_BASE_MOVER", 500, 3, 0, ""),
    BUILDING("FG_HQ1", "NFHQT1L0", "ACTOR_FG_HEADQUARTERS_1", 1200, 0, "|MF_RESOURCE_BASE"),
    BUILDING("FG_HQ2", "NFHQT2L0", "ACTOR_FG_HEADQUARTERS_2", 2400, 0, ""),
    BUILDING("FG_HQ3", "NFHQT3L0", "ACTOR_FG_HEADQUARTERS_3", 3600, 0, ""),
    BUILDING("FG_BARRACKS", "NFUTF1L0", "ACTOR_FG_TRAINING_FACILITY_1", 750, 0, ""),
    BUILDING("FG_ADV_BARRACKS", "NFUTF2L0", "ACTOR_FG_TRAINING_FACILITY_2", 1500, 0, ""),
    BUILDING("FG_VEHICLE_FACTORY", "NFVCY1L0", "ACTOR_FG_VEHICLE_FACTORY_1", 1000, 0, ""),
    BUILDING("FG_ADV_VEHICLE_FACTORY", "NFVCY2L0", "ACTOR_FG_VEHICLE_FACTORY_2", 2000, 0, ""),
    BUILDING("FG_HOVER_FACTORY", "NFHSP1L0", "ACTOR_FG_HOVER_FACTORY", 600, 0, ""),
    BUILDING("FG_REPAIR_BAY", "NFREP1L0", "ACTOR_FG_REPAIR_BAY", 600, 0, ""),
    BUILDING("FG_PHASE_FACTORY_1", "NFPHF1L0", "ACTOR_FG_PHASE_FACTORY_1", 1000, 0, ""),
    BUILDING("FG_PHASE_FACTORY_2", "NFPHF2L0", "ACTOR_FG_PHASE_FACTORY_2", 2000, 0, ""),
    BUILDING("FG_CAMERA_TOWER", "NCCAM1L0", "ACTOR_FG_CAMERA_TOWER", 150, 0, ""),
    BUILDING("FG_LIFE_PLANT", "NCLNC1L0", "ACTOR_FG_LIFE_PLANT", 1300, 0, ""),
    BUILDING("FG_POWER_PLANT", "NCPOW1L0", "ACTOR_FG_POWER_PLANT", 1450, 0, ""),
    BUILDING("FG_REFINERY", "NFRRM1L0", "ACTOR_FG_REFINERY", 800, 0, ""),
    BUILDING("FG_BRIDGE_H", "NCSBH1L0", "ACTOR_FG_SMALL_HORIZONTAL_BRIDGE", 400, 0, ""),
    BUILDING("FG_BRIDGE_V", "NCSBV1L0", "ACTOR_FG_SMALL_VERTICAL_BRIDGE", 400, 0, ""),
    BUILDING("FG_BRIDGE_C", "NCSBC1L0", "ACTOR_FG_SMALL_CENTRE_BRIDGE", 400, 0, ""),
    BUILDING("FG_GUARD_TOWER", "NFGDT1L0", "ACTOR_FG_GUARD_TOWER", 400, 10, "|MF_ATTACK"),
    BUILDING("FG_ADV_GUARD_TOWER", "NFAGT1L0", "ACTOR_FG_ADVANCED_GUARD_TOWER", 550, 13, "|MF_ATTACK"),
    BUILDING("FG_AA_SITE", "NFAAR1L0", "ACTOR_FG_AA_SITE", 600, 40, "|MF_ATTACK"),
};

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: dr_info_gen <dark-reign-root> <info.h> <info.c>\n");
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/deftxt/UNITS.TXT", argv[1]);
    char *units = read_text(path);
    snprintf(path, sizeof(path), "%s/deftxt/BUILD.TXT", argv[1]);
    char *buildings = read_text(path);
    if (!units || !buildings) {
        fprintf(stderr, "dr_info_gen: cannot read retail DEFTXT under %s: %s\n", argv[1], strerror(errno));
        free(units);
        free(buildings);
        return 1;
    }
    int count = (int)(sizeof(entries) / sizeof(*entries));
    for (int i = 0; i < count; ++i) {
        char lower[32];
        snprintf(lower, sizeof(lower), "%s", entries[i].asset);
        for (char *p = lower; *p; ++p)
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        if (!strstr(units, lower) && !strstr(buildings, lower)) {
            fprintf(stderr, "dr_info_gen: native definitions do not reference %s\n", lower);
            free(units);
            free(buildings);
            return 1;
        }
    }
    free(units);
    free(buildings);
    if (!write_info_h(argv[2], "retail DEFTXT and the OpenDR sprite catalog", entries, count) ||
        !write_info_c(argv[3], "retail DEFTXT and the OpenDR sprite catalog", "dr_types.h",
                      "SELECTION_STYLE_BRACKETS", true, entries, count)) {
        fprintf(stderr, "dr_info_gen: cannot write output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
