---
title: Rustfront
permalink: /rustfront/
---

# Rustfront field manual

Rustfront is an original real-time strategy game about three factions fighting over ore in the badlands. Build a base, keep its power online, and fight across canyons, fords, and high ground. The three factions share the same economy and have different forces, tools, and tech paths.

| Faction | Play style | Signature tools |
| --- | --- | --- |
| [Directorate]({{ '/rustfront/directorate/' | relative_url }}) | A balanced industrial army that grows into heavy armour and siege | APC transport, Field Crane repairs, Phantom phase tank, and Hammer artillery |
| [Scrap Clans]({{ '/rustfront/clans/' | relative_url }}) | Cheap, fast forces with demolition and area denial | Wreckers, Sapper mines, and private ore Derricks |
| [Lumen Concord]({{ '/rustfront/concord/' | relative_url }}) | Costly specialists that work best together | Rites, salvage, Cantor healing, and an Aegis shield |

## Start a game

```sh
make build/bin/rustfront
build/bin/rustfront
```

Choose **Campaign** for one of three five-mission stories, **Skirmish** for AI seats and team selection, or **Multiplayer** to host or join a LAN match. See the [campaign and map guide]({{ '/rustfront/modes/' | relative_url }}) for objectives, maps, and saves.

For a quick match, run `build/bin/rustfront --map maps/skirmish.txt`. The `triad.txt`, `triad-clans.txt`, and `triad-concord.txt` maps put you in command of each faction in a three-way fight.

## First base

1. Select the headquarters. Build a power structure, then a refinery and barracks. Clicking a structure's build row arms placement. Click clear ground to begin construction.
2. Protect the refinery's free harvester. It finds ore on its own when idle and returns cargo to the nearest friendly refinery. A field cell contains 1,000 ore. A harvester gathers 50 ore a second with a 500-ore load. Depleted cells turn to sand.
3. Build a vehicle factory once your refinery is ready. Train units by clicking their build rows. Repeat a click to add copies to the queue, up to nine. Select a barracks or factory and click the ground to set a rally point.
4. Watch the power meter. If draw exceeds supply, construction and unit production run at half speed and turrets fire at half rate. Check the faction pages for each structure's power draw or output.
5. Send scouts ahead of siege weapons. Artillery and the Hammer can reach beyond their own sight. Aircraft cross ground obstacles and require an anti-air weapon to engage.

The standard side starts with 5,000 credits. Individual maps can override it.

## Controls

| Input | Action |
| --- | --- |
| Left click or drag | Select. With units selected, click a destination or target to order them. |
| Right click | Deselect |
| Ctrl+1–9, then 1–9 | Store, then recall a control group. Recall it twice to centre the camera. |
| Ctrl+A | Select all owned units |
| Alt+left click | Force move |
| Ctrl+left click | Attack-move |
| WASD, arrow keys, or middle drag | Pan |
| R or Repair | Toggle repairs for selected structures |
| X or Sell | Sell selected structures |
| Z, C, V | Activate the shown context commands or available Rites |
| Escape | Pause and open the in-game menu |

The radar pans the view. Build rows show prices, availability, queue and progress. When the Concord has a card available, choose a Rite and click the map for targeted effects.

## Combat and upkeep

- Fog starts black. Explored ground outside sight dims and holds a memory of the last known enemy buildings. Spotting determines which enemies the side can engage.
- Detectors reveal enemy mines. Every headquarters and fixed defensive tower detects, as do the Sentry Post and Cantor. The roster tags identify aircraft weapons and detectors separately.
- Three kills promote a unit to veteran and six to elite. Veterans deal 20% more damage. Elites deal 40% more and slowly recover health.
- Repair restores 5% of maximum structure HP per second and charges half the corresponding share of its build price. Selling returns up to half of the build price, scaled by remaining health.
- Water, rocks, wrecks, and cliff faces block ground travel and construction. Look for ramps and fords to move between elevations.

[Map and mission authoring]({{ '/engine/rustfront-maps/' | relative_url }}) covers custom scenarios, terrain keys, and game data.
