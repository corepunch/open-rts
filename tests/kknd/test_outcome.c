#include "engine.h"
#include "info.h"
#include "t_local.h"

/* KKnD keeps every engine default for level outcomes. */

#define CHECK(c) RTS_CHECK(c, "engine level outcome", #c)
#include "../outcome_regression.h"

int main(void) {
    if (outcome_cases(MT_SURV_RIFLEMAN)) return 1;
    puts("PASS: elimination victory and defeat, allies, partial rules, network teams, kills, losses, teams, result screen");
    return 0;
}
