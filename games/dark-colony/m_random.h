#ifndef __M_RANDOM__
#define __M_RANDOM__
#include <stdint.h>
extern const uint32_t dc_random_table[256];

uint32_t M_DC_Random(uint8_t *index);
uint32_t P_DC_Random(void);
#endif
