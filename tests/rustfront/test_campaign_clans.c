/* Every mission of this campaign is won by meeting its objectives and lost by losing the
 * protected thing or the whole side. "--play=clans-N" has the bot play mission N for real
 * (make test-rustfront-campaign). */
#include "campaign_harness.h"

int main(int argc, char **argv) { return campaign_faction_main(argc, argv, RF_CLANS); }
