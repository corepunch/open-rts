# Dark Colony AI Verification Guide

This guide documents the AI verification process for multiplayer and singleplayer play, based on reverse-engineering analysis of retail DC.EXE.

## Automated Verification (Tests)

### Core Tests (Existing)
- `test_ai_skirmish.c` — Singleplayer AI building, attacking, income scaling (✅ passes)
- `test_ai_interface.c` — Engine rules, ladder priority, timing (✅ passes)
- `test_ai_team_aware.c` — Shared AI interface (✅ passes)

### New Multiplayer Tests
- `test_ai_multiplayer_verification.c` — 3-way FFA, 4-way teams, alliance enforcement, income scaling
- `test_ai_edge_cases.c` — Harvester loss recovery, credit starvation, sustained defense

Run all tests:
```sh
make test-dark-colony
```

Run a single test:
```sh
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_ai_multiplayer_verification
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_ai_edge_cases
```

## Manual Verification (Interactive Play)

### Test 1: Singleplayer Skirmish (Human vs AI)
**Map:** D2PLAY01 (smallest, fastest setup)  
**Setup:** Human (race 0) vs AI (race 1)  
**Expected:** 
- AI builds Brozaar harvester (~30 seconds)
- AI tech-ups to Warfold hive (~2 minutes)
- AI fields combat units (Greys, Demons)
- AI launches attack waves (~4-5 minutes)
- Human takes casualties if passive

**What to watch:**
- ✅ AI's harvester reaches a vent and mines
- ✅ AI builds buildings (hives, sensor pods)
- ✅ Combat units spawn from barracks/hives
- ✅ AI attacks with groups, not single units
- ❌ AI shouldn't attack its own city
- ❌ AI shouldn't move all harvesters at once (leaves mining inactive)

---

### Test 2: Multiplayer Skirmish (Human + Ally vs Enemy)
**Map:** D4PLAY01 (4-player)  
**Setup:**
- Player 1: Human, Team 0, Race 0
- Player 2: AI, Team 0, Race 0 (allied with human)
- Player 3: AI+, Team 1, Race 1 (enemy)
- Player 4: AI+, Team 1, Race 1 (enemy)

**Expected:**
- AI allies build independently but focus on team 1
- AI+ players get 2x income, stronger economy
- No friendly fire between allies
- Human doesn't see AI ally attacking their own units

**What to watch:**
- ✅ Ally AI attacks enemies (team 1), not you
- ✅ Both AI+ players build faster than AI
- ✅ Team coordination: waves against common enemy
- ❌ Ally AI shouldn't target your units
- ❌ Mixed race teams shouldn't break AI

---

### Test 3: Income Scaling (AI vs AI+)
**Map:** D2PLAY01  
**Setup:** Two separate games
- Game A: Human (passive) vs AI, race 0
- Game B: Human (passive) vs AI+, race 0

**Steps:**
1. Buy exactly one harvester in each game (manually)
2. Disable all AI except economy: `level.exo_income[1] = 0`
3. Let them mine for 6 minutes
4. Compare total credits earned

**Expected:** AI+ earns ~2x credits of AI from same mining  
**Verified from DC.EXE at 0x412d8e:** `amount * [player+0xe20] >> 8`
- AI stores 0x100 at offset 0xe20
- AI+ stores 0x200 at offset 0xe20
- DC Caveat: offset 0x401741 may override this in singleplayer war mode

**What to watch:**
- ✅ Both generate positive income
- ✅ AI+ accumulates credits faster (visible in resource bar)
- ❌ Income shouldn't drop or stall

---

### Test 4: Alliance System (No Friendly Fire)
**Map:** D8PLAY01 (8-player)  
**Setup:**
- Player 1: Human
- Player 2: AI, Team 1
- Player 3: AI+, Team 1 (allied with player 2)
- Player 4: AI, Team 2
- Players 5-8: Empty

**Expected:**
- Players 2 & 3 attack player 4, never each other
- Player 4 can attack both but only holds one front
- Player 1 can attack/defend any

**What to watch:**
- ✅ After 10 minutes: no wounds on allied AI units from each other
- ✅ Both allied AIs attack the lone player
- ✅ AI learns about alliances, doesn't need manual diplomacy
- ❌ Friendly fire → alliance system is broken
- ❌ Allies gang up on player → wrong enemy detection

---

### Test 5: Defense Response
**Map:** D2PLAY01  
**Setup:** Human vs AI+  
**Steps:**
1. Trigger a human attack near AI's base
2. Keep units there for 30 seconds

**Expected:** AI rallies defenders to intercept  
**Verified from DC.EXE:** Defense check in dispatcher loop at ~0x419a54

**What to watch:**
- ✅ AI defenders spawn/rally toward threat
- ✅ Threat disappears, defenders relax (return to duty/standby)
- ✅ New threat = new rally
- ❌ AI ignores attackers at base
- ❌ Defenders wander aimlessly (no target)

---

### Test 6: Both Races Balance
**Map:** D2PLAY01  
**Setup:** Run two games
- Game A: Human (race 1) vs AI (race 0)
- Game B: Human (race 0) vs AI (race 1)

**Expected:** Similar AI spending pattern regardless of race  
**Both races have:**
- 1x harvester unit
- Different production buildings (Barracks/Hive)
- Unique combat units (Trooper/Grey, Reaper/Demon)
- Balanced cost/power trade-off

**What to watch:**
- ✅ Both races build harvesters, barracks/hives, combat units
- ✅ Wave timing/size similar between races
- ✅ Total unit count converges (not race-dependent)
- ❌ One race builds nothing (ladder broken for that race)
- ❌ One race hoards credits while the other spends

---

## Edge Cases & Recovery

### Harvester Loss
**Setup:** Singleplayer vs AI  
**Steps:**
1. Let AI build one harvester
2. Kill it (cheat or wait for enemy)
3. Watch credits bar

**Expected:** 
- Credits slow momentarily (no harvest)
- AI buys replacement within 2-3 minutes
- Mining resumes

**What to watch:**
- ✅ AI doesn't give up (keeps spending on army)
- ✅ New harvester purchased and deployed
- ✅ Temporary income pause → recovery
- ❌ Permanent credit freeze (stuck harvester state)
- ❌ Cascade failures (loses all units)

---

### Singleplayer War Mode Caveat
**Reference:** AI_REVIEW_2026-09.md section "DC.EXE comparison"

DC.EXE has a second loop at offset 0x401741 that rewrites the income multiplier (0xe20) from an unknown setup array. If this array contains 0x64 (100) for every slot in single-player war, AI+ would **not** get the 2x bonus.

**Test:** Long singleplayer AI+ game (20+ minutes)
- Does income multiplier hold constant?
- Or does it drop to AI-level after some time?

**Current status:** Long-run tests show multiplier holds steady (no override detected).

---

## Ladder Structure

Both races follow a linear progression:

### Terran (Race 0)
1. Harvester (Exploiter)
2. Production building (Barracks)
3. Combat unit (Trooper, count 3)
4. Second harvester (Exploiter)
5. Tech building (Scipod)
6. Combat unit (Trooper, count 6)
7. Advanced tech (Roboftr)
8. Advanced units (Reaper, count 3)
... (continues)

### Alien (Race 1)
1. Harvester (Brozaar)
2. Production building (Warfold)
3. Combat unit (Grey, count 3)
4. Second harvester (Brozaar)
5. Tech building (Breedpod)
6. Combat unit (Grey, count 6)
7. Advanced tech (Genesac)
8. Advanced units (Demon, count 3)
... (continues)

**Known gaps (not in ladder):**
- Aircraft (Trooper upgrade, Sentinel, or Reaper upgraded variants)
- Research upgrades (in DC.EXE but not ported)
- Expansion beyond start city (single-city strategy)
- Scouting or fog-of-war reasoning
- Retreat or base relocation

---

## Comparison Against Retail DC.EXE

Disassembled findings (confirmed correct):
- **Dispatcher:** 4-tick AI think cadence, staggered per owner (DC.EXE 0x419a54)
- **Income scaling:** Multiplier stored at player+0xe20 (0x100 AI, 0x200 AI+)
- **Credit math:** `earned * multiplier >> 8` applied at 0x412d8e
- **Squad structure:** Four vtable pointers per AI, custom targeting code (NOT ported)

Our ladder strategy is **not** a transcription of retail squad behavior—it's an original policy with similar observable structure (think cadence, economy first, tech up, steady army, waves).

---

## Troubleshooting

**AI not building:**
- Check `tests/dark-colony/test_ai_skirmish.c` — if it passes, AI is functional
- In-game: pause, check map fog for AI units
- Check event log: does AI have `AI_EVENT_PURCHASE` events?

**Friendly fire occurring:**
- Check alliance teams in skirmish setup
- Run `test_ai_multiplayer_verification` — alliance test should catch this
- Verify unit ownership color matches expected team

**Income doesn't scale:**
- Check `level.income_scale[owner]` at game load
- Verify `P_ScaleIncome()` is called in credit deposit path
- Run `test_ai_plus_income()` — exact ratio check

**AI attacks too weak/strong:**
- Adjust `wave_min_size / wave_max_size` in `dc_ai_plan()` (games/dark-colony/p_ai.c:76-78)
- Adjust `wave_interval_ms` for attack timing
- Check ladder counts (UI_TROOPER, UI_GRAY, etc.)

**AI seems smart then stupid:**
- Currently plays with global knowledge (no fog of war)
- No retreat or multi-base strategy
- These are acceptable simplifications for RTS gameplay
