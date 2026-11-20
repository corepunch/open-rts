#!/usr/bin/env python3
"""Generate Dark Reign's damage tables from retail DEFTXT.

Reads DAMAGE.TXT (vulnerability classes by offense), UNITS.TXT and BUILD.TXT
(SetType, SetVulnerability, AddWeapon) and WEAPON.TXT (SetOffense,
PersistentDamage, CanShootFlyer/GroundUnit/Building) and writes a C header.

Usage: dr_damage_gen.py <dark-reign-root> <out.h>
"""
import re
import sys


def blocks(text, keyword):
    """Yield (name, body) for every `keyword(name) { ... }` block."""
    lines = [l.split(';', 1)[0] for l in text.splitlines()]
    i = 0
    while i < len(lines):
        m = re.match(r'\s*' + keyword + r'\(\s*([^)\s]+)([^)]*)\)', lines[i])
        if not m:
            i += 1
            continue
        header = (m.group(1) + ' ' + m.group(2)).split()
        depth, body, started = 0, [], False
        i += 1
        while i < len(lines):
            line = lines[i]
            depth += line.count('{') - line.count('}')
            if '{' in line:
                started = True
            body.append(line)
            i += 1
            if started and depth <= 0:
                break
        yield header, body


def read(root, name):
    with open(f'{root}/deftxt/{name}', encoding='latin-1') as f:
        return f.read()


def main():
    root, out = sys.argv[1], sys.argv[2]
    damage = read(root, 'DAMAGE.TXT')
    offenses = re.search(r'DeclareOffenses\(\)\s*\{([^}]*)\}',
                         '\n'.join(l.split(';', 1)[0] for l in damage.splitlines())).group(1).split()

    classes = []
    for header, body in blocks(damage, 'DefineVulnerability'):
        name, dv, dvar, dcrit = header[0], *map(int, header[1:4])
        rows = {o: (dv, dvar, dcrit) for o in offenses}
        for line in body:  # in file order; a bare letter covers each category of it
            parts = line.replace('{', ' ').replace('}', ' ').split()
            if len(parts) != 4:
                continue
            key, vals = parts[0], tuple(map(int, parts[1:]))
            for o in offenses:
                if o == key or (not key[-1].isdigit() and o[0] == key):
                    rows[o] = vals
        classes.append((name, [rows[o] for o in offenses]))
    class_index = {name: i for i, (name, _) in enumerate(classes)}

    weapons = {}
    for header, body in blocks(read(root, 'WEAPON.TXT'), 'DefineWeapon'):
        w = {'offense': None, 'strength': 0, 'area': 0.0, 'persist': None, 'self_destruct': False}
        for line in body:
            m = re.search(r'SetOffense\(\s*(\w+)\s+(-?[\d.]+)\s+(-?[\d.]+)', line)
            if m:
                w['offense'], w['strength'], w['area'] = m.group(1), int(float(m.group(2))), float(m.group(3))
            if re.search(r'SetMovement\(\s*SelfDestruct', line, re.I):
                w['self_destruct'] = True
            m = re.search(r'PersistentDamage\(\s*(\d+)\s+(\d+)\s+(\w+)\s+(-?\d+)\s+(-?[\d.]+)', line)
            if m:
                w['persist'] = (int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), float(m.group(5)))
        weapons[header[0]] = w

    types = []
    for text, kw in ((read(root, 'UNITS.TXT'), 'DefineUnitType'),
                     (read(root, 'BUILD.TXT'), 'DefineBuildingType')):
        for header, body in blocks(text, kw):
            name = header[0]
            t = {'name': name, 'type': None, 'defense': None, 'strength': 100, 'weapons': [], 'parts': [],
                 'hitsize': 0}
            for line in body:
                m = re.search(r'SetType\((\d+)\)', line)
                if m and t['type'] is None:
                    t['type'] = int(m.group(1))
                m = re.search(r'SetVulnerability\(\s*(\w+)\s+(\d+)', line)
                if m:
                    t['defense'], t['strength'] = m.group(1), int(m.group(2))
                m = re.search(r'SetHitSize\(\s*(\d+)', line)
                if m:
                    t['hitsize'] = int(m.group(1))
                m = re.search(r'AddWeapon\(\s*(\w+)', line)
                if m:
                    t['weapons'].append(m.group(1))
                m = re.search(r'ActivePart\(\s*(\w+)', line)
                if m:
                    t['parts'].append(m.group(1))
            if t['type'] is not None and t['defense'] in class_index:
                types.append(t)
    # Training mirrors (SetType 40000..40999) swap in only for tutorials.
    types = [t for t in types if not 40000 <= t['type'] < 41000]
    # A turret building fires its ActivePart unit's weapons.
    by_name = {t['name'].lower(): t for t in types}
    for t in types:
        if not t['weapons']:
            for part in t['parts']:
                if part.lower() in by_name:
                    t['weapons'] = by_name[part.lower()]['weapons']
                    break
    # One row per native type: a building wins over its own turret part.
    unique = {}
    for t in types:
        if t['type'] not in unique or t['parts']:
            unique[t['type']] = t
    types = list(unique.values())

    def cat(o):
        return offenses.index(o) if o in offenses else -1

    with open(out, 'w') as f:
        f.write('/* Generated by tools/dr_damage_gen.py from retail DEFTXT. Do not edit by hand. */\n')
        f.write('#ifndef DR_DAMAGE_TABLE_H\n#define DR_DAMAGE_TABLE_H\n\n')
        f.write(f'#define DR_OFFENSES {len(offenses)}\n#define DR_DEFENSE_CLASSES {len(classes)}\n\n')
        f.write('enum {\n' + ''.join(f'    DR_OFF_{o},\n' for o in offenses) + '};\n\n')
        f.write('static const char *const dr_offense_names[DR_OFFENSES] = {\n    ' +
                ', '.join(f'"{o}"' for o in offenses) + ',\n};\n\n')
        f.write('static const char *const dr_defense_names[DR_DEFENSE_CLASSES] = {\n' +
                ''.join(f'    "{n}",\n' for n, _ in classes) + '};\n\n')
        f.write('/* DAMAGE.TXT: vulnerability %, variation %, critical-hit % by class and offense. */\n')
        f.write('static const dr_vulnerability_t dr_vulnerability[DR_DEFENSE_CLASSES][DR_OFFENSES] = {\n')
        for name, rows in classes:
            f.write(f'    {{ /* {name} */\n')
            for o, (v, var, crit) in zip(offenses, rows):
                f.write(f'        {{ {v}, {var}, {crit} }}, /* {o} */\n')
            f.write('    },\n')
        f.write('};\n\n')
        f.write('/* Each native type: SetVulnerability class and strength, and the offense\n'
                ' * of its first weapon (-1: unarmed). */\n')
        f.write('static const dr_defense_t dr_defenses[] = {\n')
        for t in sorted(types, key=lambda t: t['type']):
            ground = -1
            for wn in t['weapons']:
                w = weapons.get(wn)
                if w and w['offense'] is not None:
                    ground = cat(w['offense'])
                    break
            f.write(f'    {{ {t["type"]}, {class_index[t["defense"]]}, {t["strength"]}, {ground} }}, /* {t["name"]} */\n')
        f.write('};\n\n')
        f.write('/* Each armed native type: its first weapon\'s SetOffense area, SetHitSize,\n'
                ' * PersistentDamage(times cycles offense strength area) and a\n'
                ' * SetMovement(SelfDestruct) weapon (offense strength area). World units,\n'
                ' * 24 a cell. */\n')
        f.write('static const dr_weapon_t dr_weapons[] = {\n')
        for t in sorted(types, key=lambda t: t['type']):
            first = next((weapons[w] for w in t['weapons'] if w in weapons and weapons[w]['offense']
                          and not weapons[w]['self_destruct']), None)
            sd = next((weapons[w] for w in t['weapons'] if w in weapons and weapons[w]['self_destruct']), None)
            if not first and not sd and not t['hitsize']:
                continue
            area = int(first['area']) if first else 0
            p = first['persist'] if first and first['persist'] else (0, 0, None, 0, 0)
            pc = cat(p[2]) if p[2] else -1
            sdv = (cat(sd['offense']), sd['strength'], int(sd['area'])) if sd else (-1, 0, 0)
            f.write(f'    {{ {t["type"]}, {area}, {t["hitsize"]}, {{ {p[0]}, {p[1]}, {pc}, {p[3]}, {int(p[4])} }}, '
                    f'{{ {sdv[0]}, {sdv[1]}, {sdv[2]} }} }}, /* {t["name"]} */\n')
        f.write('};\n\n#endif\n')


if __name__ == '__main__':
    main()
