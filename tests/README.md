# open-rts Test Suite

## Structure

```
tests/
    rts_test.h           — Tiny shared test macros (RTS_CHECK, RTS_RUN, rts_fail)
    rts_model_test.h     — Shared header-only model test helpers (rts_tick, rts_find_unit, etc.)
    dark-colony/         — Dark Colony headless model tests (auto-discovered test_*.c)
    dark-reign/          — Dark Reign headless model tests (auto-discovered test_*.c)
    7legion/             — 7th Legion headless model tests (auto-discovered test_*.c)
    kknd/                — KKnD headless model tests (auto-discovered test_*.c)
    cross-game/          — Dual-plugin / cross-game integration tests (e.g., test_model_commands.c)
    test_dark_colony_sprite_layout.c — Sprite layout data alignment test
```

## Writing Tests

1. Create a new `test_*.c` file under `tests/<game>/`.
2. `#include "rts_model_test.h"` (or `"rts_test.h"` for non-model tests).
3. No Makefile updates required — `make test-<game>` will automatically discover, compile, link, and run it.

## Running Tests

```sh
# Run all test suites
make test

# Run tests for a specific game
make test-dark-colony
make test-dark-reign
make test-7legion
make test-kknd

# Run model command lifecycle tests across games
make test-model-commands
```
