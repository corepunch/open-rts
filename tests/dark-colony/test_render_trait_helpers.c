#include "engine_config.h"
#include "../rts_model_test.h"
#include <stdio.h>

static int test_find_helpers(void) {
    RtsRenderSnapshot snapshot = {0};
    snapshot.unit_count = 2;
    snapshot.units[0].owner = 0;
    snapshot.units[0].type_id = 10;
    snprintf(snapshot.units[0].sprite_name, sizeof(snapshot.units[0].sprite_name), "SPRITES/TROOPER.SPR");

    snapshot.units[1].owner = 1;
    snapshot.units[1].type_id = 20;
    snprintf(snapshot.units[1].sprite_name, sizeof(snapshot.units[1].sprite_name), "SPRITES/GRAY.SPR");

    RTS_CHECK(rts_find_unit(&snapshot, 0, 10) == 0, "test_render_trait_helpers", "rts_find_unit owner 0 type 10");
    RTS_CHECK(rts_find_unit(&snapshot, 1, 20) == 1, "test_render_trait_helpers", "rts_find_unit owner 1 type 20");
    RTS_CHECK(rts_find_unit(&snapshot, 0, 99) == -1, "test_render_trait_helpers", "rts_find_unit non-existent");

    RTS_CHECK(rts_find_unit_with_sprite(&snapshot, "SPRITES/TROOPER.SPR") == 0, "test_render_trait_helpers", "rts_find_unit_with_sprite trooper");
    RTS_CHECK(rts_find_unit_with_sprite(&snapshot, "SPRITES/GRAY.SPR") == 1, "test_render_trait_helpers", "rts_find_unit_with_sprite gray");
    RTS_CHECK(rts_find_unit_with_sprite(&snapshot, "SPRITES/MISSING.SPR") == -1, "test_render_trait_helpers", "rts_find_unit_with_sprite missing");

    printf("PASS: rts_find_unit / rts_find_unit_with_sprite\n");
    return 0;
}

int main(void) {
    RTS_RUN(test_find_helpers());
    return 0;
}
