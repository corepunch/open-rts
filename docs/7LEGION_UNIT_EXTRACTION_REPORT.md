# 7th Legion Unit Data Extraction Report

**Date**: 2026-09-11  
**Source**: Asset analysis, GameText.txt, Missions.ini, filename patterns  
**Method**: No executable disassembly; text config + asset filename analysis only

## Summary

Extracted unit information from non-binary sources:
- **7 core units**: Fully verified with implemented stats
- **13+ additional unit types**: Inferred from GameText.txt, asset filenames, mission data
- **0 direct stat values**: No secondary sources (other than the 7 core units) contain HP/speed/damage

---

## Currently Implemented (7 Units)

All verified from working code + asset existence + production table:

| ID | Type Name | Asset | HP | Speed | Damage | Role |
|----|-----------|-------|----|----|--------|------|
| 1 | MT_TROOPER | GFX/LTROOP.BIM | 100 | 4 | 15 | Infantry, attack |
| 2 | MT_SLAVE | GFX/SLAVEN1.BIM | 60 | 4 | 0 | Harvester |
| 3 | MT_SPIDER_MECH | GFX/SPIDER.BIM | 300 | 3 | 35 | Mech, attack |
| 4 | MT_TANK | GFX/TANKBASE.BIM | 500 | 5 | 50 | Heavy, attack |
| 5 | MT_ROCK_MECH | GFX/ROCKMECH.BIM | 800 | 3 | 70 | Heavy, attack |
| 6 | MT_TRUCK | GFX/TRUCK.BIM | 200 | 5 | 0 | Transport/harvester |
| 7 | MT_MOBILE_BASE | GFX/MOBBASE.BIM | 1000 | 3 | 0 | Base, producer |

---

## Inferred Unit Types (From GameText.txt Encyclopedia)

### Lines 109-117: Heavy/Mech Units
These appear to be tank/mech class units:
- **109**: Marauder
- **110**: Oppressor  
- **111**: Crucifier
- **112**: APC (Armored Personnel Carrier)
- **113**: Tormentor (possibly Mech variant, suggests aggression)
- **114**: Avenger
- **115**: Faith Hammer
- **116**: Annihilator
- **117**: Purifier

### Lines 120-128: Infantry/Specialist Units
- **120**: infantry (generic)
- **121**: Machine Gunner
- **123**: Mortar Unit
- **124**: Priest (special, possibly unit boost?)
- **125**: Medic
- **126**: Slaven Rider
- **127**: Marine
- **128**: Commander

### Lines 140-145, 150: Advanced Units
- **140**: Dominator
- **141**: Obliterator
- **142**: Light M*ch (Light Mech, spelled with asterisk in original)
- **143**: Nova
- **144**: Venom Typhoon
- **145**: Redeemer
- **149**: Mobile Base (already implemented as #7)
- **150**: Pyroclast

---

## Asset-Based Unit Mapping

### Infantry/Trooper Variants
Assets found: LTROOP.BIM, BTROOP.BIM, CTROOP.BIM, GTROOP.BIM, ITROOP variants  
**Interpretation**: Color/team variants of the same unit  
**Likely stats**: Match MT_TROOPER (100 HP, speed 4, dmg 15)

### Tank Variants
Assets found: 27+ files including:
- TANKBASE.BIM, TANKTOP.BIM (base model)
- ATTANK.BIM (maybe "Advanced Tank"?)
- GHTANK, GLTANK, GMTANK (Green/Gray variants?)
- LBTANK, MBTANK, MTTANK (Light/Medium/Medium-type tank?)
- LTTANK, HBTANK, HTTANK (Light Tech/Heavy variants?)
- NTANK (possibly "New" or "Neutron"?)

**Current mapping**: TANKBASE = MT_TANK (500 HP, speed 5, dmg 50)  
**Possible expansion**: 4-6 tank variants with different HP/damage profiles

### Mech/Heavy Units  
Assets found:
- ROCKMECH.BIM (implemented as MT_ROCK_MECH)
- SPIDER.BIM (implemented as MT_SPIDER_MECH)
- HEAVYBOT.BIM (potentially new unit)
- MECH1* variants (body/head/leg parts?)

**Likely**: 2-4 mech unit types with varying HP (300-1000) and damage (35-100)

### Specialized Units
Assets found:
- JEEP + JEEPGUN variants (light vehicle)
- DINO1W.BIM (possibly dinosaur unit or placeholder?)
- MORTAR*.BIM (mortar weapon/unit?)

---

## Mission Data Analysis (PVStart Strings)

The `PVStart` fields in Missions.ini encode starting units as 43-character digit strings.  
**Format**: Likely position-based or grouping-based (each position/pair = unit count)

### Non-zero positions observed:
- Position 2: Values 1 (appears in many missions)
- Position 4: Values 1-3 (common)
- Position 6: Values 2-3 (common)
- Position 7: Value 2
- Position 8: Value 1
- Position 14-15: Values 1-4 (high variability suggests important unit slots)
- Position 20: Value 3 (notable)
- Position 33-34, 38: Rare (only in 2-3 missions)
- Position 42: Mostly 1 (final position)

**Cannot decode without exe reference**: The exact mapping of position → unit type requires understanding how the engine parses this 43-char string. This could be:
1. One digit per unit type (43 unit types max)
2. Pairs of digits (21 unit types, each pair = count 0-99)
3. Variable-length groups with delimiters
4. Bit-packed encoding

---

## Category Suggestions for New Units

Based on asset naming + gameplay patterns + GameText:

### Light Infantry (100-150 HP, speed 6-8, dmg 10-20)
- Machine Gunner, Sniper/Marine, Medic

### Medium Mechs (200-400 HP, speed 3-4, dmg 30-50)
- APC, Light Mech, Priest(?), Mortar Unit

### Heavy Tanks (400-700 HP, speed 4-5, dmg 40-80)
- Marauder, Oppressor, Crucifier, Tormentor, Avenger

### Super Heavy (800+ HP, speed 2-3, dmg 60-100)
- Annihilator, Dominator, Obliterator, Pyroclast, Venom Typhoon

### Commander/Special (Varies)
- Commander, Redeemer, Faith Hammer, Nova, Purifier

---

## Data That CANNOT Be Extracted Without Exe

- **Exact unit type → PVStart position mapping**
- **Damage type interactions** (armor values, piercing vs impact, etc.)
- **Animation frame counts per action** (stand, walk, attack, death)
- **Attack range in game tiles**
- **Turret rotation speed**
- **Weapon reload timing details**
- **Build time in milliseconds** (only "Build Time: 200" in Encyclopedia with no unit context)
- **Production prerequisites** (which buildings produce which units)
- **Unit AI behavior params** (aggression, pursuit range, etc.)

---

## Recommendations

### Option A: Extend with Inferred Stats (Conservative)
Add 2-3 additional units with reasonable stat progression:
```c
{ // MT_JEEP (Light vehicle)
  .doomednum = 8, .spawnstate = S_JEEP_STND, .spawnhealth = 150,
  .speed = 8, .damage = 20, .radius = 16, .height = 32, .mass = 100,
  .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
},
{ // MT_HEAVYBOT (Super Heavy)
  .doomednum = 9, .spawnstate = S_HEAVYBOT_STND, .spawnhealth = 1200,
  .speed = 2, .damage = 100, .radius = 16, .height = 32, .mass = 150,
  .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
},
```

### Option B: Create Stub Entries
Define all 25+ named units from GameText with placeholder stats that scale logically:
```c
// Use naming convention to auto-derive stats:
// Light (L): 150 HP, 6 speed, 15 dmg
// Medium (M): 400 HP, 4 speed, 40 dmg
// Heavy (H): 800 HP, 3 speed, 70 dmg
// Super (S): 1200 HP, 2 speed, 100 dmg
```

### Option C: Full Reverse Engineering
Disassemble legion.exe to find:
- Unit property tables
- PVStart parser function
- Damage/armor calculation
- Building → unit production mappings

---

## Next Steps

1. ✅ **Extracted**: Unit names, asset existence, mission patterns
2. ⚠️ **Cannot extract safely without exe**: HP/dmg/speed for new units beyond the 7 implemented
3. 🎯 **Suggested path**: 
   - Keep current 7 verified units
   - Add 2-3 high-confidence units (Jeep, HeavyBot) with balance-appropriate stats
   - Document as "inferred from asset naming + gameplay balance" not "extracted from source data"
