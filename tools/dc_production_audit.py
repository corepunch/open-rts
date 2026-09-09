#!/usr/bin/env python3
"""Audit retail BUILDSTAND/BUILD lookup; run from the repository root.

Requires make dc-info-conv. Prints native metadata only, not runtime state
configuration. See docs/DC_EXE_FINDINGS.md, production-channel audit.
"""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path('data/DCOLONY')
lines = [line.strip() for line in (root / 'GAMESTAT/GAMESTAT.TXT').read_text().splitlines()
         if line.strip() and not line.lstrip().startswith('%')]
unit_count = int(lines[0])
units = [line.split() for line in lines[1:unit_count + 1]]
assert len(units) == unit_count and all(len(unit) >= 33 for unit in units)

labels = {}
for name in (root / 'ANIM.DAT').read_text().splitlines():
    path = root / 'ANIMATE' / name.strip().upper()
    if not path.is_file():
        raise ValueError(f'Missing ANIM.DAT entry: {path}')
    data = json.loads(subprocess.check_output(['build/dc_info_conv', '--labels', str(path)]))
    for label in data['labels']:
        if 'BUILD' in label['name']:
            if label['name'] in labels:
                raise ValueError(f'Ambiguous label: {label["name"]}')
            labels[label['name']] = path, label

rows = []
for native_type, unit in enumerate(units):
    prefix = unit[0]
    # 0x438c95..0x438d16 prefers BUILDSTAND as a family; availability at
    # 0x4385a8 probes even numeric suffixes, with no implicit prefix aliases.
    found = []
    for action in ('BUILDSTAND', 'BUILD'):
        found = [labels[prefix + action + str(facing)] for facing in range(0, 32, 2)
                 if prefix + action + str(facing) in labels]
        if found:
            break
    sequences = []
    for path, label in found:
        data = json.loads(subprocess.check_output(
            ['build/dc_info_conv', '--label', label['name'], str(path)]))
        frames = data['frames']
        assert len(frames) == label['end'] - label['start'] + 1
        sequences.append({
            'file': str(path.relative_to(root)), 'label': label,
            'raw_ticks': [frame['ticks'] for frame in frames],
            'first_commands': frames[0]['commands'],
            'last_commands': frames[-1]['commands'],
        })
    rows.append({'native_type': native_type, 'prefix': prefix,
                 'exit_variant': int(unit[23]), 'sequences': sequences})

print(json.dumps({
    'exe_sha256': hashlib.sha256((root / 'DC.EXE').read_bytes()).hexdigest(),
    'units': rows,
    'unmatched_build_labels': [label for name, (_, label) in labels.items()
                               if not any(name.startswith(unit[0] + 'BUILD') for unit in units)],
}, indent=2))
