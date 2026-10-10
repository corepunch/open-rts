#define TECH_STARTERS { MT_FG_CONSTRUCTION_CREW }
/* The Freedom Guard's Water Contaminator needs the third HQ and the advanced
 * vehicle factory, which need both second-tier buildings, which need the HQ. */
#define TECH_CASES { \
    { 10004, 1, { 10001 } }, \
    { 10002, 3, { 10001, 10004, 10006 } }, \
    { 30, 7, { 10001, 10004, 10006, 10002, 10007, 10005, 10003 } }, \
}
#include "../tech_path_regression.h"
