#include "engine.h"

/* 7th Legion keeps native column-major grids: reject older row-major save bodies. */
uint32_t G_SaveLayout(void) { return 1; }
