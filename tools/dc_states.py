#!/usr/bin/env python3
"""Export raw per-FIN C state tables from authored gameplay policy and FIN data.

Run from the repository root after make dc-info-conv. --check verifies that
checked-in output is current without writing it.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('data/DCOLONY')
GAME = Path('games/dark-colony')
BUILDINGS = ('EXCOPOD', 'BRRKPOD', 'ROBOPOD', 'ROBOPOD2', 'SCNCPOD', 'SCNCPOD2', 'RSCHPOD')
# Family          action                 group  terminal  timing
# Explicit engine policy: FIN supplies pictures/delays, never C function names.
FAMILY_RULES = (
    ('BLOOD',     'NULL',                 0, 'S_NULL', 'blood'),
    ('SCRCH',     'A_DC_BuildingStand',    1, 'loop',   'loop'),
    ('BURN',      'A_DC_BuildingStand',    1, 'loop',   'loop'),
    ('DIE',       'NULL',                 4, 'S_NULL', 'death'),
)
TERMINAL_STATES = (
    ('S_PRODUCTION_READY', 'SPR_HUBU', 66, 0, 'A_DC_ProductionReady', 'S_NULL', 6),
)


def numbers(text, count):
    if '..' in text:
        first, last = map(int, text.split('..'))
        result = list(range(first, last + 1))
    else:
        result = list(map(int, text.split(',')))
        if len(result) == 1:
            result *= count
    if len(result) != count:
        raise ValueError(f'{text}: expected {count} values')
    return result


def authored_states():
    for line in Path('tools/dc_states.txt').read_text().splitlines():
        line = line.split(';')[0].strip()
        if not line:
            continue
        name, sprite, frames, tics, action, nextstate, group = line.split()
        match = re.fullmatch(r'(\w+)\{(\d+)\.\.(\d+)\}', name)
        names = ([match[1] + str(i) for i in range(int(match[2]), int(match[3]) + 1)]
                 if match else [name])
        for i, (frame, ticks) in enumerate(zip(numbers(frames, len(names)), numbers(tics, len(names)))):
            yield (names[i], 'SPR_' + sprite, frame, ticks, action,
                   names[i + 1] if i + 1 < len(names) else nextstate, int(group))


def native_catalog():
    for path in sorted((ROOT / 'ANIMATE').glob('*.FIN')):
        result = subprocess.run(['build/dc_info_conv', str(path)], capture_output=True, text=True)
        if result.returncode:
            # The former blood exporter likewise skipped unsupported FIN layouts.
            print(f'dc_states: skipping unsupported {path}: {result.stderr.strip()}', file=sys.stderr)
            continue
        data = json.loads(result.stdout)
        spr = ROOT / 'SPRITES' / (path.stem + '.SPR')
        cells = 0
        if spr.exists():
            with spr.open('rb') as file:
                header = file.read(4)
            if len(header) != 4:
                raise ValueError(f'{spr}: truncated header')
            cells = int.from_bytes(header[2:4], 'little')
        yield path.stem, cells, data


def family_states(sprite, cells, data, label, rule):
    _, action, group, terminal, timing = rule
    first, last = label['start'], label['end']
    name = label['name']
    elapsed = total = 0
    for frame in range(first, last + 1):
        if timing == 'blood' and frame == first + 1:
            elapsed = total = 0  # Blood skips reset frame zero at startup.
        raw = data['frames'][frame]['ticks']
        ticks = (((raw or 15) + 3) * 15 // 100) & 255
        total += 1 if timing == 'death' and frame == first else (ticks or 256)
        boundary = (total * 66 * 30 + 500) // 1000
        nextstate = (f'S_{name}_{frame + 1}' if frame < last else
                     f'S_{name}_{first}' if terminal == 'loop' else terminal)
        yield (f'S_{name}_{frame}', 'SPR_' + sprite, cells + frame,
               boundary - elapsed, action, nextstate, group)
        elapsed = boundary


def generate():
    catalog = list(native_catalog())
    rows = [('S_NULL', '0', 0, -1, 'NULL', 'S_NULL', 0), *authored_states()]
    blood = []
    for sprite, cells, data in catalog:
        for label in data['labels']:
            if 'BLOOD' in label['name']:
                blood.append(f'    {{ "{label["name"]}", S_{label["name"]}_{label["start"]} }},\n')
                rows.extend(family_states(sprite, cells, data, label, FAMILY_RULES[0]))
    building = []
    loaded = {Path(name.strip()).stem.upper() for name in (ROOT / "ANIM.DAT").read_text().splitlines()}
    for prefix in BUILDINGS:
        ranges = []
        for rule in FAMILY_RULES[1:]:
            name = prefix + rule[0] + '0'
            matches = [(s, c, d, label) for s, c, d in catalog for label in d['labels']
                       if s in loaded and label['name'] == name]
            if len(matches) != 1:
                raise ValueError(f'{name}: expected one native label, got {len(matches)}')
            sprite, cells, data, label = matches[0]
            rows.extend(family_states(sprite, cells, data, label, rule))
            ranges.append(f'{{ "{name}", S_{name}_{label["start"]}, S_{name}_{label["end"]} }}')
        building.append('    { ' + ', '.join(ranges) + ' },\n')
    rows.extend(TERMINAL_STATES)
    names = [row[0] for row in rows]
    if len(set(names)) != len(names):
        raise ValueError('Duplicate state names')
    for name, sprite, frame, tics, action, nextstate, group in rows:
        if nextstate not in names:
            raise ValueError(f'{name}: unknown next state {nextstate}')
    files = defaultdict(list)
    for name, sprite, frame, tics, action, nextstate, group in rows[1:]:
        files[sprite.removeprefix('SPR_')].append(
            f'    [{name}] = {{ {sprite}, {frame}, {tics}, {action}, {nextstate}, {group} }},\n')
    banner = '/* Generated by tools/dc_states.py. Do not edit. */\n'
    outputs = {GAME / 'animate' / (stem + '.inc'): banner + ''.join(lines)
               for stem, lines in sorted(files.items())}
    outputs[GAME / 'blood_labels.inc'] = banner + ''.join(blood)
    outputs[GAME / 'building_sequences.inc'] = banner + ''.join(building)
    h = (GAME / 'info.h').read_text()
    start, end = h.index('    S_NULL,'), h.index('    NUMSTATES', h.index('    S_NULL,'))
    outputs[GAME / 'info.h'] = h[:start] + ''.join(f'    {name},\n' for name in names) + h[end:]
    c = (GAME / 'info.c').read_text()
    start = c.index('const state_t states[NUMSTATES] = {')
    end = c.index('\n};', start) + 3
    outputs[GAME / 'info.c'] = (c[:start] + 'const state_t states[NUMSTATES] = {\n'
        '    { 0, 0, -1, NULL, S_NULL, 0 },\n' + ''.join(
            f'#include "animate/{stem}.inc"\n' for stem in sorted(files)) + '};' + c[end:])
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs = generate()
    stale = []
    for path, text in outputs.items():
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(str(path))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    if stale:
        raise SystemExit('Stale generated files: ' + ', '.join(stale))
    print(f'{"Checked" if args.check else "Generated"} {len(outputs)} files')


if __name__ == '__main__':
    main()
