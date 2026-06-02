# common

Shared game model and simulation code lives here: maps, actors, sprites,
pathing, plugin metadata, and format-agnostic engine helpers.

Keep this directory independent of platform presentation code where possible.
The current `engine.h` umbrella still exposes client-facing prototypes during
the migration.
