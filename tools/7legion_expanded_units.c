/* 7th Legion expanded unit catalog
   Source: Asset analysis + GameText.txt encyclopedia + Mission.ini patterns
   Verified units (7): extracted from working code + asset existence
   Proposed units (4): inferred from asset names + balance extrapolation
*/

#define MOBILE(type, sprite, asset, id, hp, speed, damage, extra) \
    { type, sprite, asset, id, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra }

static const info_entry_t entries[] = {
    /* === VERIFIED UNITS (extracted from current implementation) === */

    MOBILE("TROOPER", "LTROOP", "GFX/LTROOP.BIM", "1", 100, 4, 15, "|MF_ATTACK"),
    MOBILE("SLAVE", "SLAVEN1", "GFX/SLAVEN1.BIM", "2", 60, 4, 0, "|MF_HARVESTER"),
    MOBILE("SPIDER_MECH", "SPIDER", "GFX/SPIDER.BIM", "3", 300, 3, 35, "|MF_ATTACK"),
    MOBILE("TANK", "TANKBASE", "GFX/TANKBASE.BIM", "4", 500, 5, 50, "|MF_ATTACK"),
    MOBILE("ROCK_MECH", "ROCKMECH", "GFX/ROCKMECH.BIM", "5", 800, 3, 70, "|MF_ATTACK"),
    MOBILE("TRUCK", "TRUCK", "GFX/TRUCK.BIM", "6", 200, 5, 0, "|MF_HARVESTER"),
    MOBILE("MOBILE_BASE", "MOBBASE", "GFX/MOBBASE.BIM", "7", 1000, 3, 0, ""),

    /* === PROPOSED ADDITIONS (inferred from assets) === */
    /* Source: Asset names suggest JEEP, HEAVYBOT variants.
       Stats extrapolated from balance around existing units:
       - Jeep: Light, fast, lower damage (fills speed gap)
       - HeavyBot: Ultra-heavy, slow, high damage (above Rock Mech)
    */

    MOBILE("JEEP", "JEEPBODY", "GFX/JEEPBODY.BIM", "8", 150, 7, 20, "|MF_ATTACK"),
    MOBILE("HEAVYBOT", "HEAVYBOT", "GFX/HEAVYBOT.BIM", "9", 1200, 2, 100, "|MF_ATTACK"),

    /* === ASSET VARIANTS (team/color recolors, reuse mobjinfo) === */
    /* These could reference the same mobjinfo as their base type,
       only changing sprite asset. Examples:
       - BTROOP.BIM → uses MT_TROOPER mobjinfo
       - CTROOP.BIM → uses MT_TROOPER mobjinfo
       - ATTANK.BIM → uses MT_TANK mobjinfo
       Not yet added pending clarification of color variant role.
    */
};

/* Alternative categorization of GameText encyclopedia entries:

   Lines 109-117 (Heavy units, likely tank variants):
     109: Marauder    (tank class?)
     110: Oppressor   (tank class?)
     111: Crucifier   (tank class?)
     112: APC         (medium unit)
     113: Tormentor   (mech class?)
     114: Avenger     (medium-heavy?)
     115: Faith Hammer (special?)
     116: Annihilator (super-heavy)
     117: Purifier    (special?)

   Lines 120-128 (Infantry/support):
     120: infantry     (MT_TROOPER?)
     121: Machine Gunner
     123: Mortar Unit
     124: Priest       (support/special)
     125: Medic        (support)
     126: Slaven Rider (MT_SLAVE variant?)
     127: Marine       (infantry variant)
     128: Commander    (hero unit?)

   Lines 140-145 (Advanced units):
     140: Dominator      (super-heavy tank?)
     141: Obliterator    (super-heavy mech?)
     142: Light M*ch     (light mech, 200-300 HP expected)
     143: Nova           (unknown role)
     144: Venom Typhoon  (heavy ranged?)
     145: Redeemer       (special/support?)
     150: Pyroclast      (super-heavy?)
*/
