---
title: Campaigns and maps
permalink: /rustfront/modes/
---

# Campaigns and maps

## Campaign

Forty years after the collapse, power-bearing crystals grow in the old foundry belt. The Directorate wants its industrial holdings back. The Scrap Clans fight to keep the ground they learned to live on. The Lumen Concord seeks the pieces of the First Lens.

Choose a faction and select a territory marker to read the briefing and objectives. Each faction has five missions. Winning a mission takes its territory and unlocks the next one. After the battle, the debrief tallies time, production, credits, kills, and losses and awards a rank.

| Mission | Directorate · The Iron Mandate | Scrap Clans · Rust and Fury | Lumen Concord · The Lumen Path |
| --- | --- | --- | --- |
| 1 | **Foothold** · Establish a base and bank 4,000 credits | **Scrap Rights** · Establish a base and hoard 3,000 credits | **First Light** · Establish a base and gather 3,000 credits |
| 2 | **Relay Station** · Protect the relay for five minutes | **Smash and Grab** · Burn the depot and keep the Warboss alive | **Shard Recovery** · Hold the stone ring for 90 seconds |
| 3 | **The Ford** · Hold the ford and burn the War Camp | **Junkyard Siege** · Keep the Clan Hall alive for six minutes | **Vigil** · Keep the Beacon lit for six minutes |
| 4 | **Glasswater** · Destroy the Spire on the mesa | **Snuff the Light** · Destroy the gate tower and Fusion Core | **Breaking the Mandate** · Destroy the War Factory and Command Center |
| 5 | **Iron Dawn** · Destroy the Clans and the Concord | **Scrapfall** · Raze the Iron Hill | **Clear Sky** · Destroy both factions and keep the Lens lit |

Read the [campaign briefings and story](https://github.com/corepunch/open-rts/blob/main/games/rustfront/docs/CAMPAIGN.md) for mission background. Campaign completion is tracked separately from mid-mission save slots.

## Skirmish and LAN

In **Skirmish**, pick a map and assign the open seats to yourself, AI players, or closed slots. Each active seat has a faction or Random and a team. Difficulty affects the AI economy, army size, and timing. Easy earns less per load and never repairs. Hard earns more and attacks earlier with larger waves.

In **Multiplayer**, one player hosts and the others join the lobby. Select each seat's faction and team before launching. The game places the faction's headquarters and starting force at that seat's map start.

| Lobby map | Players | Grid | Layout |
| --- | ---: | --- | --- |
| Ironbridge | 2 | 64 × 40 | River canyon with three fords and ore on each mesa |
| Mesa Duel | 2 | 56 × 56 | Ore-rich central mesa with a ramp on every face |
| Three Crowns | 3 | 71 × 64 | Central plateau, ravines, and expansions between the three bases |
| Crossroads | 4 | 72 × 56 | Crossing canyons, fords, and a central ore plaza |
| Highland Ring | 4 | 64 × 64 | Highland starts overlooking a low ring and lake |

Direct-launch maps under `maps/` include the pre-placed `skirmish.txt` duel, each faction's variant of `triad.txt` and `duel.txt`, and `badlands.txt`, a terrain showcase with canyon crossings and a mesa.

## Saves and settings

Single-player games have eight save slots. The pause menu opens objectives, Save, Load, and Options. The user directory holds `rustfront-N.sav` files, `campaign.txt` for progress, and `settings.cfg` for effects volume, music volume, and game speed. Set `OPEN_RTS_USER_DIR` to choose that directory. Otherwise the game uses the SDL preference path.
