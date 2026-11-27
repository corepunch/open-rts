---
title: Home
permalink: /
description: The open-rts engine documentation and Rustfront field manual.
---

# open-rts

A shared C engine for sprite-based real-time strategy. Use the engine guide to follow a command from input through a simulation tic, or learn how to bring up a new game. Use the Rustfront manual to pick a faction, plan an opening, and look up a unit or building.

[Engine guide]({{ '/engine/' | relative_url }}){: .btn .btn-primary }
[Rustfront field manual]({{ '/rustfront/' | relative_url }}){: .btn }

<img class="manual-hero" src="{{ '/rustfront/img/battlefield.webp' | relative_url }}" alt="Rustfront battle map with industrial buildings, tracked units, and a production sidebar" width="1280" height="960" loading="lazy">

## Build and play

From the repository root, with a C toolchain, Make, pkg-config, and SDL2 installed:

```sh
make build/bin/rustfront
build/bin/rustfront
```

Rustfront is the original game included in the repository. The retail integrations for StarCraft, Dark Colony, Dark Reign, 7th Legion, KKnD, Warcraft II, and Z load files from a locally supplied copy. See the [contribution guide](https://github.com/corepunch/open-rts/blob/main/CONTRIBUTING.md) for per-title data paths.

## Documentation

| Guide | Covers |
| --- | --- |
| [Engine architecture]({{ '/engine/' | relative_url }}) | Simulation, data ownership, rendering, UI, and networking |
| [Adding a game]({{ '/engine/adding-a-game/' | relative_url }}) | A first playable level, original content, and retail integration |
| [Rustfront]({{ '/rustfront/' | relative_url }}) | Controls, economy, campaigns, maps, factions, and the complete roster |
