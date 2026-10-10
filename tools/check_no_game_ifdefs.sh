#!/bin/sh
# Shared engine code must not branch on which game is being built. Game
# identity belongs to games/<id>/ (and to tests/, which build one game each);
# the engine reads data from gameinfo_t or calls weak game hooks instead.
# The Makefile may still pass -DRTS_GAME_<ID> to the compiler.
set -u
cd "$(dirname "$0")/.." || exit 2
pattern='RTS_GAME_(DARK_COLONY|DARK_REIGN|7LEGION|KKND|WARCRAFT_2|STARCRAFT)\b'
dirs="driver render hud interface play game sound include"
status=0
for dir in $dirs; do
    [ -d "$dir" ] || continue
    if grep -rnE "$pattern" "$dir" --include='*.c' --include='*.h' --include='*.inc'; then
        status=1
    fi
done
if [ "$status" -ne 0 ]; then
    echo "check-ifdefs: RTS_GAME_<ID> used in shared code (see above)." >&2
    echo "Use gameinfo_t data or a hook declared in include/engine.h instead." >&2
    exit 1
fi
echo "check-ifdefs: ok"
