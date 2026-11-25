# Rustfront (working title)

An original RTS for open-rts: a desert industrial faction with team-coloured
buildings and vehicles, in the spirit of the late-90s pre-rendered RTS look.
So far there is only unit art, modelled in Scener, and the sprites rendered
from it.

| Buildings | Vehicles |
|---|---|
| Command Center (3x3), Power Plant (2x2), Ore Refinery (3x2), Barracks (2x2), War Factory (3x3), Gun Turret (1x1, rotating head) | Scout Buggy, Battle Tank (rotating turret), Self-propelled Artillery, Ore Harvester, APC (rotating turret) |

![Lineup](art/previews/Lineup.png)

- `art/render.sh` renders the previews above.
- `art/sprites.py` renders the game sprites into `sprites/`: 16 facings for
  vehicles, separate turret layers, team-colour masks and ground shadows, with
  pivots in `sprites.json`.
- `art/contact.py` composites them as a check (`art/previews/sprites_contact.png`).

See [docs/SCENER.md](docs/SCENER.md) for the conventions and both pipelines.
