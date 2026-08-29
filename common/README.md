# common

Shared game model and simulation code lives here: maps, actors, sprites,
pathing, declarative UI definitions, plugin metadata, and format-agnostic engine
helpers. Game-specific folders provide `GameUiDefinition` data and asset/file
adapters; the client performs the shared UI, terrain, and sprite rendering.

Keep this directory independent of platform presentation code where possible.
The current `engine.h` umbrella still exposes client-facing prototypes during
the migration.
