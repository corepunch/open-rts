#!/usr/bin/env python3
"""Compare loader catalogs across revisions, or run small malformed-file fixtures.

Run from the checkout containing the retail data directory. --source-tree may
point at a separate worktree: the same harness then tests that revision's code
against the same data and emits comparable text fingerprints.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
GAMES = (
    ("dark-colony", "DC", None),
    ("dark-reign", "DR", "w_spr.c"),
    ("7legion", "SL", "w_bim.c"),
    ("kknd", "KK", "w_spr.c"),
)


def manifests():
    result = {}
    for game, root, pattern in (
        ("dark-colony", "data/DCOLONY/SCENARIO", "*.MAP"),
        ("dark-reign", "data/REIGN/dark/scenario", "*.SCN"),
        ("7legion", "data/7LEGION", "MAPT.000"),
        ("kknd", "data/KKND/LEVELS/640", "*.LVL"),
    ):
        result[game, "maps"] = [str(p) for p in sorted(Path(root).rglob(pattern))]
    sprites = []
    for path in sorted(Path("data/REIGN/dark/graphics").rglob("*.FTG")):
        data = path.read_bytes()
        if data[:4] != b"BOTG":
            continue
        offset, count = struct.unpack_from("<ii", data, 4)
        for index in range(count):
            name, start, size = struct.unpack_from("<28sii", data, offset + 36 * index)
            if name.split(b"\0")[0].lower().endswith(b".spr"):
                sprites.append(f"{path}|{start},{size}")
    result["dark-reign", "sprites"] = sprites
    result["7legion", "sprites"] = [
        str(p) for p in sorted(Path("data/7LEGION/GFX").glob("*.BIM"))
        if not p.name.startswith("TILES")
    ]
    # Includes unsupported/absent members, whose rejection must also stay stable.
    result["kknd", "sprites"] = [f"{index}.mobd" for index in range(100)]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-tree", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--fixtures", action="store_true")
    parser.add_argument("--no-build", action="store_true", help="use binaries/objects already built by make")
    args = parser.parse_args()
    if not args.fixtures and not args.output:
        parser.error("--output is required for a catalog run")
    source = args.source_tree.resolve()
    if not args.no_build:
        subprocess.run(["make", "-j8", "-C", str(source)], check=True, stdout=subprocess.DEVNULL)
    sdl = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2"], text=True).split()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    catalogs = {} if args.fixtures else manifests()
    with tempfile.TemporaryDirectory(prefix="loader-tests-") as temporary:
        output = args.output or Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        for game, define, included in GAMES:
            sources = sorted(p for directory in ("driver", "game", "play", "render", "interface", "hud", f"games/{game}")
                             for p in (source / directory).rglob("*.c")
                             if p.name != "d_main.c" and p.name != included)
            objects = [source / "build" / game / p.relative_to(source).with_suffix(".o") for p in sources]
            binary = Path(temporary) / game
            command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-O2", "-g", f"-D{define}",
                       f"-DRTS_WORLD_Y_UP={int(game == 'dark-colony')}"]
            command += [f"-I{source / directory}" for directory in
                        (".", "driver", "game", "play", "render", "interface", "hud", f"games/{game}")]
            if args.fixtures:
                command.append("-DLOADER_FIXTURES")
            command += [str(ROOT / "tests/loader_catalog.c"), *map(str, objects), *sdl, "-lm", "-o", str(binary)]
            subprocess.run(command, check=True)
            if args.fixtures:
                subprocess.run([str(binary), "--fixtures"], env=env, check=True)
            else:
                for (catalog_game, kind), paths in catalogs.items():
                    if catalog_game != game:
                        continue
                    manifest = output / f"{game}-{kind}.manifest"
                    manifest.write_text("\n".join(paths) + "\n")
                    with (output / f"{game}-{kind}.txt").open("w") as out, (output / f"{game}-{kind}.log").open("w") as err:
                        subprocess.run([str(binary), kind, str(manifest)], env=env, stdout=out, stderr=err, check=True)
                    print(f"{game}: {len(paths)} {kind} -> {output / (game + '-' + kind + '.txt')}", flush=True)


if __name__ == "__main__":
    main()
