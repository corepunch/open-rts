# Rustfront (working title)

An original RTS for open-rts with three factions, in the spirit of the late-90s
pre-rendered RTS look. Like StarCraft's races they share an economy but differ
in look, roster and tempo:

- **Directorate**: chunky industrial buildings painted in the team colour, a
  balanced army with the twin-gunned Mastodon, and the Hammer's long-range missiles at the top.
- **Scrap Clans**: salvage, rust and corrugated sheet. Cheap, fast and many;
  flamethrowers, gun trucks and rocket rigs; frequent waves; Wreckers that drive their
  drums into your base and go up with it.
- **Lumen Concord**: white ceramic, cyan light and hover craft. Few, dear and
  strong; beam weapons, beam towers, late and decisive attacks behind an Aegis's shield.
Its units are modelled in Scener, rendered to sprites, packed to 256 colours,
and loaded by its own open-rts binary.

Every faction fills the same roles (`factions.c`): a headquarters (3x3) that builds the
structures, power (2x2), a refinery (3x2, comes with a harvester), a barracks (2x2), a vehicle
factory (3x3) and a turret (1x1, rotating head). Each also has a tech structure (2x2) that
unlocks its top-tier unit.

| Faction | Buildings | Vehicles | Infantry |
|---|---|---|---|
| Directorate | Command Center, Power Plant, Ore Refinery, Barracks, War Factory, Gun Turret, Uplink Centre, (Sentry Post) | Scout Buggy, Battle Tank, Artillery, APC (transport), Mastodon (two guns), Hammer (missiles), Ore Harvester, Field Crane, Phantom, Kestrel (gunship), Sentry Truck | Rifleman, Rocket Soldier |
| Scrap Clans | Clan Hall, Generator, Smelter, War Camp, Scrap Yard, Spike Tower (twin MG), Chop Shop, (Derrick) | Trike, Gun Truck, Scrap Crawler, Rocket Rig (three-rocket salvo), Wrecker (demolition), Scrap Hauler, Drill Rig, Crusher, Buzzard (autogyro) | Raider, Torcher (flamethrower), Sapper (mines) |
| Lumen Concord | Spire, Fusion Core, Extractor, Academy, Foundry, Beam Tower (splashing beam), Sanctum | Skimmer, Glaive, Prism (splashing heavy beam), Aegis (shield and repair), Hover Harvester, Strider, Seraph (flyer) | Warden, Lancer, Cantor (priest) |

**The top tier.** Each tech structure comes from the headquarters once the factory stands,
draws a lot of power, and unlocks one unit at the factory:

| Tech structure | Unit | What it does |
|---|---|---|
| **Uplink Centre** (Directorate, 1500): a team-painted blockhouse under a lattice mast with a big turning satellite dish; it sees 12 cells round the base | **Hammer** (1800): an eight-wheeled carrier with a twin launcher raised toward the front | Fires a pair of heavy missiles that arc and splash, from 11 cells, every 6.5 s; it cannot fire inside 2.5 cells, and sees only 6, so it needs spotters |
| **Chop Shop** (Scrap Clans, 1000): a rusty corrugated garage under a team tarp, a derrick swinging a wrecked car, a fire barrel | **Wrecker** (600): a fast buggy with a spiked ram and a chained stack of explosive drums | Drives into its target and goes up: 500 to the target, half to every other enemy within two cells. Shot dead on the way it still goes up, for a third |
| **Sanctum** (Lumen Concord, 1600): a white ceramic shrine of four leaning fins round a cyan obelisk, under a floating, turning halo | **Aegis** (1400): a broad hover hull with an emitter dish in a crown of fins | Its field (3.5 cells) takes a third off every hit on its side's units and mends their vehicles 12 hit points a second; its own beam is light |

**Three classics.** Beyond the shared roles, each faction plays like a different late-90s RTS,
in its mechanics rather than its looks (`g_units.c`):

| Faction | After | Unit | What it does |
|---|---|---|---|
| Directorate | Dark Reign | **APC** (700) | Carries five soldiers: LOAD sends the selected soldiers into the nearest APC with a seat, UNLOAD lets them out round it. Aboard they cannot be seen or shot; if the APC is destroyed they bail out at half strength |
| | | **Field Crane** (700) | Unarmed; each second welds 30 hit points onto the most damaged vehicle of its side within two cells (60 onto a structure) |
| | | **Phantom** (1100) | A phase tank: the first hit it takes shifts it out of phase for 3 s (it flickers, nothing can target or hurt it, it cannot fire), then it needs 20 s to recharge |
| | | **Kestrel** (1200, needs the Uplink) | A helicopter gunship: flies over everything, fires pairs of rockets at the ground, and only anti-air weapons reach it |
| | | **Sentry Truck** (650) | DEPLOY unfolds it into a **Sentry Post** on its cell, a twin machine gun that shoots up too and sees hidden things; PACK UP folds it back |
| Scrap Clans | KKnD | **Drill Rig** (1200) | DEPLOY sets a **Derrick** down on the 2 x 2 cells it stands over (not on ore): a private well of 6000 ore that only its side's haulers draw from |
| | | **Sapper** (250) | A soldier with a shotgun who, holding ground for 6 s, lays a mine on his cell (16 a side, a cell and a half apart). Mines are hidden from the enemy unless a detector sees them, and the first enemy to drive or walk over one takes 180 and its neighbours 90 |
| | | **Crusher** (750) | A monster truck that rolls over every enemy soldier under its wheels and rams the rest |
| | | **Buzzard** (900, needs the Chop Shop) | A scrap autogyro that drops drums of explosive (120, splashing) on the ground; it cannot hit aircraft |
| Lumen Concord | 7th Legion | **The Rites** | Power cards: a Spire deals one every 40 s into a hand of three, shown under the build list. **Strike**: a pillar of light lands 1.5 s after it is called, 350 to every enemy within 1.5 cells. **Mending**: its side's units within 3 cells get back half their hit points (structures a quarter). **Windfall**: 750 credits. **Warding**: its side's units within 3 cells take no damage for 8 s |
| | | **Tithe** | Everything the Concord destroys pays it a fifth of its price |
| | | **Strider** (1200) | A spider mech whose arc leaps from its target to two more enemies nearby |
| | | **Seraph** (1300, needs the Sanctum) | A flying beam craft that hits aircraft and the ground alike |
| | | **Cantor** (350) | A priest: each second mends 10 hit points on each of the three most hurt soldiers of its side within 3 cells, and sees hidden things |

Aircraft are reached only by anti-air weapons: rifles, rockets and machine guns (riflemen, rocket
soldiers, the Scout Buggy, the APC, the Sentry Post, raiders, trikes, gun trucks, the Spike Tower,
the Rocket Rig) and the lighter beams (wardens, lancers, skimmers, the Beam Tower, the Aegis, the
Seraph). Cannons, artillery, flames and rams cannot. Hidden mines show to detectors: every
headquarters and defensive tower, the Sentry Post and the Cantor.

The command bar under the build list holds what the selection can do, on Z, C and V: DEPLOY,
PACK UP, LOAD, UNLOAD; with none of those selected, a Concord player sees the hand of Rites there.
A Rite played on a point (Strike, Mending, Warding) waits for a click on the map and shows the
ring it will cover. The computer deploys its trucks and rigs where they stop and plays its cards.

![The three factions' bases in game](art/previews/factions.png)

![Lineup](art/previews/Lineup.png)

- `art/render.sh` renders the previews above.
- `art/sprites.py` renders the game sprites into `sprites/`: 16 facings for
  vehicles, 8 for animated infantry, separate turret layers, team-colour masks and ground shadows, with
  pivots in `sprites.json`.
- `art/pack8.py` turns them into 256-colour PCX sheets in `assets/` with one shared
  palette and team ramps, plus `sprites.inc` for the C side.
- `art/contact.py` composites them as a check (`art/previews/sprites_contact.png`).

![In game](art/previews/ingame.png)

![Sidebar with the Command Center selected](art/previews/sidebar.png)

![A firefight and a tank going up](art/previews/combat.png)

![A War Factory going up, and one burning](art/previews/construction.png)

![Red's base four minutes in, and a red wave at Blue's ore field](art/previews/ai.png)

## Play

```sh
make build/bin/rustfront
build/bin/rustfront                    # the front end: campaign, skirmish, multiplayer
build/bin/rustfront --map maps/skirmish.txt        # Directorate against Directorate
build/bin/rustfront --map maps/triad.txt           # you lead the Directorate against the Clans and the Concord
build/bin/rustfront --map maps/triad-clans.txt     # you lead the Scrap Clans
build/bin/rustfront --map maps/triad-concord.txt   # you lead the Lumen Concord
build/bin/rustfront --map maps/badlands.txt        # every terrain, a canyon with ramps, a mesa
build/bin/rustfront --map maps/crossroads.txt      # a multiplayer map: every start a side, you the first
build/bin/rustfront --map maps/campaign/clans-1.txt  # a campaign mission: the Scrap Clans' first
build/bin/rustfront --map maps/duel.txt            # one against one on point-symmetric ground
build/bin/rustfront --map maps/duel-clans.txt      # you lead the Clans against the Concord
build/bin/rustfront --map maps/duel-concord.txt    # you lead the Concord against the Directorate
env SDL_VIDEODRIVER=dummy build/bin/rustfront --check
```

One skirmish map (`maps/skirmish.txt`, 48 x 30 cells): Blue (you) top left, Red
(the computer) bottom right, each with an ore field, and a contested field in the
middle.

`maps/duel.txt` (56 x 36, written by `maps/duel.py`) is a fair one-against-one: point-symmetric
ground, each base with an ore field beside it and another down its flank, a rich field in the
middle between gravel ridges. The balance harness plays on it.

`maps/badlands.txt` (64 x 44, written by `maps/badlands.py`) shows every terrain and the
relief: a river at the bottom of a canyon, a level below the plain, with ramps down its walls
to two gravel fords; a stone mesa a level above, with one ramp up its south face; grass
meadows, rock outcrops, scrap wrecks, and concrete pads under the Directorate's and the Clans'
bases.

**Multiplayer maps** have neutral starts instead of bases (see [Multiplayer](#multiplayer)):

| Map | Players | Size | Symmetry | Layout |
|---|---|---|---|---|
| Ironbridge (`ironbridge.txt`) | 2 | 64 x 40 | mirrored | a canyon with a river down the middle, three fords with ramps, canyon ore by the outer fords, a mesa of ore in each base's south |
| Mesa Duel (`mesa-duel.txt`) | 2 | 56 x 56 | turned 180° | a mesa of rich ore in the middle with a ramp up each face; silt basins with ponds in the empty corners |
| Three Crowns (`three-crowns.txt`) | 3 | 71 x 64 | mirrored | two bases north, one south, about 52 cells apart each; a plateau with one ramp facing each base; ravines; an expansion between every two bases |
| Crossroads (`crossroads.txt`) | 4 | 72 x 56 | mirrored both ways | a cross of canyons with rivers, fords and a plaza of ore where they meet; a mesa in every quarter; good for 2 v 2 |
| Highland Ring (`highland-ring.txt`) | 4 | 64 x 64 | turned 90° | each base on a highland with two ramps down to a ring of plain; a basin with a lake and ore in the middle |

Each is written by its own script (`maps/ironbridge.py` ...) on `maps/mp.py`, which draws the
terrain and relief, makes the map symmetric and refuses to write it unless every start stands
on flat open ground with room for its force, every start can drive to every other, and every
start has the same ore at the same distance. Rerun a script after changing it.

## Campaign

Fifteen missions, five for each faction, from a tutorial (a headquarters and a squad: build
power, a refinery and a barracks, bank some credits) through raids, sieges and a river
crossing to an all-out assault ([docs/CAMPAIGN.md](docs/CAMPAIGN.md) has the format and the
story). `maps/campaign/missions.py` writes them into `maps/campaign/`.

| | Directorate: The Iron Mandate | Scrap Clans: Rust and Fury | Lumen Concord: The Lumen Path |
|---|---|---|---|
| 1 | Foothold: build up, bank 4000 | Scrap Rights: build up, hoard 3000 | First Light: build up, gather 3000 |
| 2 | Relay Station: protect the relay 5 min | Smash and Grab: burn the depot, keep the Warboss | Shard Recovery: hold the stone ring 90 s |
| 3 | The Ford: hold the ford, burn the War Camp | Junkyard Siege: keep the Hall 6 min | Vigil: keep the Beacon lit 6 min |
| 4 | Glasswater: destroy the Spire on the mesa | Snuff the Light: the gate tower and the Fusion Core | Breaking the Mandate: the War Factory and the Command Center |
| 5 | Iron Dawn: destroy the Clans and the Concord | Scrapfall: raze the Iron Hill | Clear Sky: destroy both, keep the Lens lit |

A mission is a map with mission keys: `mission`, `title`, `briefing`, `objective` (destroy
all, destroy a tagged thing, protect one, survive, hold cells, bank credits, build something),
`tech` (what the player may build), and `trigger`s (timed, on entering cells, on a thing
destroyed, on a build) that `spawn` units, which hunt the player, or show a `message`.
`g_campaign.c` runs them and ends the level through the engine's outcome system
(`G_LevelObjectives`, `G_EndLevel`).

**The front end's API** (`rustfront.h`, 0-based indices):

| Call | Gives |
|---|---|
| `RF_CampaignName(faction)` | "Directorate", "Scrap Clans", "Lumen Concord" |
| `RF_MissionPath(faction, index, out, size)` | the map to load, `maps/campaign/clans-3.txt` |
| `RF_MissionInfo(root, faction, index, &info)` | its title, briefing (paragraphs joined by `\n`) and objectives' text |
| `RF_MissionUnlocked(faction, index)`, `RF_CampaignWon(faction)` | progress: the first mission is open, each win opens the next |
| `RF_CampaignRecordWin(faction, index)`, `RF_CampaignResetProgress()` | write it; a won mission records itself |
| `RF_InMission`, `RF_ObjectiveCount`, `RF_ObjectiveText`, `RF_ObjectiveState`, `RF_LastMissionMessage` | the running mission, for an objectives panel |

Progress is kept in `campaign.txt` under the user directory (`OPEN_RTS_USER_DIR`, else SDL's
pref path). `make test-rustfront` loads every mission and proves each is won by its objectives
and lost by losing; `make test-rustfront-campaign` has a bot play them all for real.

**Terrain** (`art/terrain.txt`, [docs/TERRAIN.md](docs/TERRAIN.md)): each map letter is a
ground type. Silt, sand, gravel, grass, stone, ore and water are procedural textures; rocks,
wrecks and concrete are modelled in Scener. Where two meet, the higher layer spills over the
lower one's edge with a ragged border, the way Dark Reign layers its ground. A map can also
give every cell a height level: where the level changes the ground breaks in a rock cliff
whose face hangs down the screen, as in KKnD, or slopes down a ramp. Water, rocks, wrecks and
cliffs block movement and building.

![Terrain in game: a mesa with its ramp, the canyon's walls, a ford and an ore island](art/previews/terrain.png)

**Fog of war.** Ground none of your units or structures has seen is black. Ground seen before
but out of sight now is dimmed, and keeps the enemy structures as you last saw them (built,
burning, or long since destroyed) until you look again. Each type sees its own distance: scouts,
trikes and skimmers far, soldiers and heavy guns near, the headquarters and the Uplink Centre
far round the base. Units only shoot what their side sees, so artillery and the Hammer need
spotters. The radar shows the same: black, dimmed, remembered structures, and only the units
you can see.

![The opening view: your base lit, the ground beyond black, the power meter and the radar's fog](art/previews/fog.png)

**Power.** Structures make power (headquarters 30, Power Plant 100, Generator 80, Fusion Core
150) or draw it (a refinery 25–30, a factory 35–50, a turret 10–30, a tech structure 40–60). The
meter at the top left of the sidebar shows drawn / made. When a side draws more than it makes,
its production and construction run at half speed and its turrets fire at half rate, until it
builds more power or sells something.

**Repair and sell.** Select your structures and press **REPAIR** (R): each mends a twentieth of
its hit points a second at half the price of those hit points; press again to stop. **SELL**
(X) takes the selected structures down for half their price, scaled by their health. A wrench
blinks over a structure under repair.

**Rally points.** With a barracks or factory selected, click the ground: a flag marks the spot,
and every unit it finishes drives there.

**Veterancy.** Kills make a unit a veteran (3, one chevron) and then elite (6, two chevrons). A
veteran does a fifth more damage and an elite two fifths more; elite units mend themselves
slowly.

**Controls:**

| Input | Does |
|---|---|
| Left click, drag | Select; with units selected, order them (move, attack what you click) |
| Right click | Deselect |
| Ctrl+1–9, 1–9 | Store a control group, recall it; recall it again to centre the view on it |
| Ctrl+A | Select everything of yours |
| R, X | Repair, sell the selected structures |
| Alt+left, Ctrl+left | Move without attacking, attack-move |
| WASD, arrows, middle drag | Pan |

**The computer** is the engine's AI (`play/p_ai.c`) playing Rustfront's faction
(`rules.c`):

- **Opening:** a C&C build order of power, barracks and riflemen, then the
  refinery and a second harvester, the War Factory, gun turrets and a first
  armoured group.
- **After the opening:** it keeps two harvesters and two turrets per base, and
  masses a mixed army of riflemen, rocket soldiers, tanks, artillery, APCs and
  scouts, up to 28 units.
- **Attack:** it attacks in waves of 6 to 16 once it matches what it has seen of
  you, and falls back when a wave is outmatched.
- **Building:** its structures rise on scaffolding like yours (`g_ai.c` picks
  sites near its Command Center with a free cell all round).
- **Upkeep:** it builds power before anything else when it is short (or within 15 of it), and
  above easy it repairs a structure under 60 % when it has credits to spare. Late in its
  opening it builds its tech structure and fields the top-tier unit.
- **Under the fog:** its picture of your army is only what its units have seen, and its units
  only shoot what they see. Like a classic C&C skirmish computer it knows where your
  structures stand (its waves march on your nearest one unscouted) and notices any enemy
  inside its base's defence radius.
- **Difficulty** (`RF_SetAIDifficulty(owner, RF_AI_EASY | RF_AI_NORMAL | RF_AI_HARD)` in
  `rustfront.h`, set before the game starts): easy collects 40 % less from each load, keeps a
  60 % army and one defence fewer, attacks half as often again with smaller waves and needs a
  bigger edge, and never repairs. Hard collects 30 % more (its only cheat), keeps a 35 %
  larger army and one more defence, attacks sooner with larger waves and at a smaller edge.

**Balance.** `make balance-rustfront` plays the computer against the computer in every faction
pairing on the duel map, 48 games each with the sides swapped and the starting credits varied,
and fails if any faction wins more than 65 % against another (`tests/rustfront/test_balance.c`;
`make test-rustfront` plays one short game per pairing). As tuned, the Directorate wins 57 %
against the Clans and the Concord 57 % against each of the others.

**Sidebar:**
- **Credits:** the counter at the top; the power meter beside it.
- **Radar:** the map in small, with team dots and your view; click it to move
  the view.
- **Build list:** the selected structure's products as cameo rows. Dark icons
  are unaffordable or still need a prerequisite. A darkening curtain shows build
  progress.
- **REPAIR, SELL and the page:** under the list.

**Production:**

| Builder | Makes | Needs |
|---|---|---|
| Command Center | Power Plant | — |
| Command Center | Barracks, Ore Refinery | Power Plant |
| Command Center | War Factory | Ore Refinery |
| Command Center | Gun Turret | Barracks |
| Command Center | Uplink Centre | War Factory |
| Barracks | Rifleman, Rocket Soldier | — |
| War Factory | Scout, Battle Tank, APC, Artillery | — |
| War Factory | Mastodon | Barracks |
| War Factory | Hammer | Uplink Centre |
| War Factory | Ore Harvester | Ore Refinery |

The Clans and the Concord follow the same pattern with their own types: the Chop Shop needs the
Scrap Yard and unlocks the Wrecker; the Sanctum needs the Foundry and unlocks the Aegis.

To build a structure, click its row, then click a clear site: it rises there on
scaffolding over its build time, one structure at a time. Click a unit's row
again to queue more, up to 9. A new refinery arrives with a free harvester.

**Economy:**
- Each side starts with 5000 credits; a map can set its own with
  `credits OWNER N`.
- Every ore cell holds 1000 ore.
- A harvester mines 50 a second up to a load (500; the Scrap Hauler 450), brings it to the
  nearest refinery, and goes back.
- An idle harvester finds the nearest ore on its own.
- A mined-out cell turns back to sand.

## Front end

`build/bin/rustfront` with no map opens the title screen (`m_menu.c`), drawn over
`assets/title.pcx` (a graded shot of the badlands with a stencilled logo, `art/title.py`) in the
game's own faces (`assets/font_*.pcx`, rendered by `art/font.py` from Aileron, the CC0 sans that
Pillow bundles).

![Title screen](art/previews/menu-main.png)

- **Campaign:** pick a faction; its five missions, locked until the one before is won, with the
  chosen one's briefing and objectives. Start plays it; afterwards the page comes back with the
  next mission open. Reset Progress clears `campaign.txt`.
- **Skirmish:** every map with starts, and for each start who plays it (you, the computer at
  easy, normal or hard, or closed), as which faction (or Random) and on which team. Start turns
  the seats into a start plan (`RF_SetStartPlan`) and difficulties (`RF_SetAIDifficulty`): you
  are owner 0, the computer players owners 1, 2, ...
- **Multiplayer:** the engine's shared host, join and lobby screens (`hud/m_net.c`), with a
  faction per seat. At launch every peer turns the published seats into the same start plan:
  person N at start N, as the faction the seat picked, on the seat's team.
- **Load Game** and, in a level, **Save Game:** eight slots, `rustfront-N.sav` under the user
  directory. A save keeps Rustfront's own state too (`g_save.c`): mined-out ore, the computer's
  difficulty and the running mission.
- **Options:** effects and music volume and game speed, kept in `settings.cfg`.
- **In a level**, Escape opens the pause panel: Resume, the mission's objectives, Save, Load,
  Options, Abandon (Leave Game in a network game) and Quit.

![Campaign](art/previews/menu-campaign.png) ![Skirmish setup](art/previews/menu-skirmish.png)

`tests/rustfront/test_front.c` drives the pages with keys and clicks, starts a mission and a
skirmish and checks the bases, alliances and difficulties the setup made, and saves and loads a
game; `OPEN_RTS_RF_UI_SHOTS=DIR` writes every page as a BMP.

## Code

| File | Role |
|---|---|
| `g_game.c` | Gameplay rows (hit points, speeds, weapons, footprints, turret and shadow sheets), identity and entry points |
| `info.h`, `info.c` | Sprite names, state runs (vehicles: stand, move, fire; infantry: idle, 6-frame walk, fire, death, corpse), mobjinfo |
| `w_terrain.c` | The terrain table (`assets/terrain.txt`): map letters, tiles, blocking, ore, the transition overlays, and the relief: levels, cliffs and ramps |
| `w_rustfront.c` | Text map loader, terrain atlas, and the sprite loader that cuts the packed BMPs into lumps with pivots, turret mounts, rotation slots, team maps and half-alpha shadows |
| `sprites.inc` | Generated by `art/pack8.py`: sheet grids, pivots, mounts and infantry animations |
| `p_prod.c` | The production catalog: costs, build times, makers, prerequisites, icons |
| `hu_rustfront.c` | The sidebar: credits, power meter, radar with fog, cameo build rows (the engine's product list does clicks, queues and placement), REPAIR and SELL |
| `factions.c` | The three factions: each one's types for the roles (headquarters, power, refinery, barracks, factory, turret, harvester), and which faction a side plays (its headquarters') |
| `g_combat.c` | Projectiles: shells, rockets, beam bolts and flame gouts do their damage on arrival; the Mastodon fires a pair of shells, the Rocket Rig a salvo of three and the Hammer a pair of heavy missiles; heavy shells, heavy bolts and heavy missiles splash; rockets trail smoke. Machine guns hit at once. The Wrecker's detonation and the Aegis's field |
| `g_units.c` | Each faction's special units and powers (Dark Reign, KKnD, 7th Legion): the APC's passengers (`TC_BOARD`, `TC_UNLOAD`), deploying (`TC_DEPLOY`), the Phantom's phase, the crane and the cantor, the Crusher, sappers and mines, anti-air, the Rites (`TC_SPELL` with no units) and the tithe, and the computer's use of them |
| `g_base.c` | Power, repair and sell, rally points and veterancy: the orders (`TC_REPAIR`, a move order to a producer) and their rules |
| `g_sight.c` | Fog of war: each type's sight, the fog pass |
| `r_marks.c` | Over each unit: selection, health, rank chevrons, the repair wrench and the rally flag |
| `g_build.c` | Construction sites that rise through four stages while their hit points grow; scorched looks below half health |
| `g_mission.c` | Footprints block movement, idle harvesters go to ore, mined cells clear, a refinery brings a harvester; impact puffs and explosions on damage and death |
| `rules.c` | Each faction's computer player: the Directorate builds up and attacks with an edge, the Clans rush with large waves, the Concord fortifies and attacks late with a clear edge; fog of war with remembered structures |
| `g_ai.c` | The computer player's hooks: it places its own structures on roomy sites, every structure anchors its defence and draws its waves, it keeps its power up and repairs, plays at a difficulty, and a harvester waits while its side has no refinery |
| `w_starts.c` | Multiplayer: start positions, the start plan, `RF_SpawnStart` and the map list |
| `g_net.c` | Lockstep: Rustfront's share of the consistency hash (terrain, blocking, alliances) |
| `s_rustfront.c` | Sound: registers the synthesised samples, maps fire, impacts, deaths, voices and announcements to them, rate-limits the under-attack warning, and plays the ambience and the player's faction's music |
| `g_campaign.c` | The campaign: mission keys, triggers, spawns that hunt the player, objectives, win and loss through `G_LevelObjectives`, progress, the front end's API |
| `m_menu.c` | The front end: title screen, campaign with briefings, skirmish setup, multiplayer lobby (the engine's), save and load, options, the in-game pause panel; the menus' faces |
| `g_save.c` | Rustfront's block in a saved game and a network resync: each computer player's difficulty, every cell's terrain (mined-out ore), then the running mission |

**Map format:** rows of terrain letters (`art/terrain.txt`: `.` sand, `:` silt, `*` ore,
`#` gravel, `,` grass, `^` stone, `~` water, `R` rocks, `x` wreck, `=` concrete), optionally
`level ROW` lines, one per row, of height levels `0`–`9` with `/` for a ramp (all 0 when
absent), then `unit NAME OWNER X Y [FACING]` lines. Each `*` cell is an ore deposit. X and Y are the cell (a building's
top-left), and FACING is 0–15, counter-clockwise from east. Owner 0 is the
player and the others are enemies, each in its team colour (blue, red, green,
yellow). A side plays the faction of its headquarters. An optional
`camera X Y` sets the opening view, and `credits OWNER N` a side's money. A `unit` line may
end in `@TAG` to name the thing for a mission, and a campaign mission adds its mission keys
([docs/CAMPAIGN.md](docs/CAMPAIGN.md)).
`title NAME` names the map for the map list, and `start N X Y [FACTION]` marks a neutral start
(see [Multiplayer](#multiplayer)).
`maps/triad.py` writes the three-way maps, `maps/duel.py` the duels, `maps/badlands.py` the
badlands and
`maps/campaign/missions.py` the campaign.

## Multiplayer

Rustfront plays over the engine's Doom-style lockstep (`driver/d_net.c`): every peer runs the
whole simulation from the same orders, and each tic they compare a hash of the world
(`G_ConsistencyVector`). Rustfront's own state is in it: each projectile's victim and each
site's progress (`MOBJ_GAME_CHECKSUM` in `mobj_data.h`), and every cell's terrain, the blocking
grid and the alliances (`G_ConsistencyExtra` in `g_net.c`).

### Starts and the lobby

A multiplayer map has no bases, only starts:

```
title Crossroads
start 0 7 7 directorate
start 1 62 7 clans
```

`start N X Y [FACTION]` is start N (0-based, without gaps) with its headquarters' top-left cell
at X, Y; FACTION (`directorate`, `clans` or `concord`) is only the default when nobody picks.
The API, in `rustfront.h`:

| Call | Does |
|---|---|
| `int RF_ListMaps(const char *root, rf_mapinfo_t *out, int max)` | Every map in `<root>/maps` (root NULL: the game's): `path` (relative to the root, e.g. `maps/crossroads.txt`), `title`, `players` (starts; 0 for a map with fixed bases), `sides` (owners with units of their own), `width`, `height`. Sorted by players, then title. Reads headers only. |
| `void RF_SetStartPlan(const rf_startplan_t *plan)` | Before loading: `plan->seat[owner]` says which start (`start`, -1: this owner does not play), which faction (`faction`, -1: the start's default) and which team (`team`, 0: alone; owners with the same non-zero team are allies). Kept until changed; NULL brings back the defaults (owner N at start N as its default faction, everyone alone). |
| `int RF_StartCount(void)`, `const rf_start_t *RF_Start(int slot)` | The loaded map's starts: `cell` and default `faction`. |
| `bool RF_SpawnStart(int owner, int faction, int x, int y)` | Places a faction's start with its headquarters at X, Y: the HQ, a harvester (it waits for the refinery), a light vehicle and a squad, facing the middle of the map. Fails, placing nothing, unless the HQ's cells are flat open ground on one level. `RF_LoadThings` calls it for every seat of the plan. |
| `int RF_FactionByName(const char *name)` | `directorate`, `clans`, `concord` to `rf_faction_id_t`; -1 otherwise. |

A map with starts is free-for-all apart from the plan's teams (`level.player_teams`), for the
computer players as well as for `P_IsAlly`. The local player's start sets the opening view.

For a network game, the front end gives every peer the same plan before it loads the map
(the spawned starts are part of the world whose hash `G_NetSignature` sends at the start, so a peer with another plan is refused before the first tic), and seats
the people as owners `0 .. numplayers-1`: `D_PlayerIsHuman` counts seats, and the computer
plays every other owner the plan names. Anything random in the lobby (a random faction or start)
must be drawn once by the host and sent, never drawn by each peer.

### Rules for lockstep-safe code

- Simulation code uses `fixed_t` and integers. Floats only for the view and the HUD.
- No `rand()`, wall-clock time or uninitialised memory in the simulation. Scatter comes from a
  hash of the tic and a mobj id (`g_mission.c`).
- Walk the thinker list, never a list ordered by pointer or by UI selection.
- `consoleplayer` is the local view only: the sidebar, the camera, sounds, results. Never the
  computer player, production or the map.
- State that steers the simulation is hashed: a new mobj field goes in `MOBJ_GAME_CHECKSUM`,
  new level state in `G_ConsistencyExtra`.
- A static cache in simulation code must not outlive a game: a peer that played a game before
  this one must compute the same.

### Desync tests

`tests/rustfront/desync_twins.h` forks two twins that play the same map from the same orders
(given to `G_RunTiccmd` as the network delivers them) and compares their consistency vectors
every tic. The second twin is another local player, has units selected, a different heap and a
game played and thrown away before; none of it may change the world.

- `test_desync`: four minutes of Ironbridge, two people mirrored: building, training,
  harvesting until ore cells run dry, down the ramps into the canyon and up the mesa, and
  fighting across the fords. Two canaries prove that a changed unit, or a changed cell of
  ground, is caught on its tic and in its subsystem.
- `test_desync_four`: Crossroads with two people and two computer players, two minutes
  (`build/bin/tests/rustfront/test_desync_four --long`: four).
- `test_starts`: the map list, every start of every map, plans, teams and empty seats.

## Sound and music

Every sound is synthesised by `art/sfx.py`, in pure Python with no third-party samples.
[docs/AUDIO.md](docs/AUDIO.md) describes the recipes and how the game uses them.
- **Weapons:** rifles, machine guns, cannon, artillery, rockets, flame and beams, heard
  from where they fire.
- **Impacts:** bullet hits, blasts, explosions and collapsing structures, heard where they land.
- **Construction:** a foundation's clunk, scaffold hammering while a site rises, and a
  chime when it finishes.
- **Announcements:** unit ready, structure ready, insufficient funds, low power, and an
  under-attack klaxon at most every 20 seconds.
- **Voices:** a wordless answer to every select, move and attack order, in each faction's
  timbre: the Directorate's clipped military radio, the Clans' gritty loudhailer, the
  Concord's crystalline chimes.
- **Ambience:** wind, and the hum of your headquarters.
- **Music:** looping tracker-style tracks: a menu theme, and in game the track of the
  faction you lead.

`make rustfront-audio` regenerates them in about a minute and relinks the game. The result
is identical every time.

## Regenerating everything

All of Rustfront's art is generated from source in this directory: Scener
prefabs (`art/prefabs/`), a detail kit (`art/kit.xml`), the palette and view
(`art/layout.py`), and Python scripts. Nothing is hand-painted. The generated
files are committed so the game builds without these tools; regenerate them
after changing any of the sources.

**Prerequisites:**
- **Scener** on `PATH`, or set `SCENER=/path/to/scener`. It is in the Orion UI
  repository: `cd ~/Developer/mapview/ui && make build/bin/scener && python3
  apps/scener/deploy.py --prefix "$HOME/.local"` puts it in `~/.local/bin`.
  Rendering needs a GL context; on macOS a terminal is enough.
- **Python 3 with Pillow** (`pip install Pillow`).
- **xmllint** (it ships with macOS).

**One command**, from the repository root, takes about 11 minutes for the three factions:

```sh
make rustfront-art
make test-rustfront
```

`make rustfront-art` runs `art/build_all.sh`, then relinks the game. In order:

| Step | Script | Makes |
|---|---|---|
| 1 | `art/layout.py`, `art/render.sh` | `art/units.blks` and the review previews in `art/previews/` |
| 2 | `art/effects.py` | Explosions, shells, rockets, smoke, plumes and fire: `sprites/fx_*.png` |
| 3 | `art/sprites.py` | Every unit's sprites: facings, animation frames, team masks, shadows, construction stages and scorched copies (`sprites/*.png`, `sprites/sprites.json`) |
| 4 | `art/icons.py` | The sidebar's build icons: `sprites/icons.png` |
| 5 | `art/pack8.py` | 256-colour PCX sheets in `assets/` and the C tables in `sprites.inc` |
| 6 | `art/contact.py` | `art/previews/sprites_contact.png`, the sprites composited as the game draws them |
| 7 | `art/sfx.py` | Every sound effect, voice, ambient loop and music track: `assets/sfx/`, `assets/music/` |

A regeneration reproduces the committed game files exactly (every PCX sheet,
`sprites.inc` and `sprites.json`). A few intermediate PNGs in `sprites/` can
differ by a single pixel of GPU rounding.

Each script can also run alone. `python3 games/rustfront/art/sprites.py tank hq`
re-renders just those units; follow it with `pack8.py`, which always repacks
everything. Scratch scenes go to `art/.build/`, which git ignores.

[docs/SCENER.md](docs/SCENER.md) explains how the art is made: conventions,
camera and lighting, animation knobs, infantry, construction and damage, icons,
the 8-bit palette, and how to add a unit. See
[docs/SCENER.md](docs/SCENER.md) for the conventions and pipelines.
