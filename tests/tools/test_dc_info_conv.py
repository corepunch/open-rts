"""Native file checks for dc_info_conv; run after make build/dc_info_conv."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile

TOOL = "build/dc_info_conv"
ROOT = Path("data/DCOLONY")


def query(*args):
    return json.loads(subprocess.check_output([TOOL, *map(str, args)]))


fin = ROOT / "ANIMATE/TRSC.FIN"
raw = fin.read_bytes()
header, frames, labels, dependencies = struct.unpack_from("<4H", raw)
lo = 8 + dependencies * 8
fo = lo + labels * 20
co = fo + frames * 164
summary = query("--labels", fin)
assert summary["header_word"] == header
assert summary["frame_count"] == frames == 472
assert len(summary["labels"]) == labels
assert not summary["frames"]
for i, label in enumerate(summary["labels"]):
    name, start, end = struct.unpack_from("<16sHH", raw, lo + i * 20)
    assert label == dict(name=name.rstrip(b"\0").decode(), start=start, end=end)
selected = query("--label", "TRSCBLOODA0", fin)
assert [f["index"] for f in selected["frames"]] == list(range(313, 323))
for f in selected["frames"]:
    n, ticks = struct.unpack_from("<HH", raw, fo + f["index"] * 164)
    assert f["ticks"] == ticks
    assert f["unknown_04"] == raw[fo + f["index"] * 164 + 4:fo + (f["index"] + 1) * 164].hex()
    part = co + sum(struct.unpack_from("<H", raw, fo + j * 164)[0] for j in range(f["index"])) * 22
    assert len(f["commands"]) == n
    for c in f["commands"]:
        sprite, cell, x, y, remap, intensity, layer, flags = struct.unpack_from("<8s7h", raw, part)
        assert c == dict(sprite=sprite.rstrip(b"\0").decode(), cell=cell, offset=[x,y],
                         remap=remap, intensity=intensity, layer=layer, flags=flags)
        part += 22
assert query("--frame", 313, fin)["frames"] == selected["frames"][:1]
assert selected["frames"][0]["native_ticks"] == 2
spr = ROOT / "SPRITES/BLOO.SPR"
raw_spr = spr.read_bytes()
for index in (0, 34, 68):
    output = query("--cell", index, spr)
    flags, cells, payload = struct.unpack_from("<HHI", raw_spr)
    w, h, x, y = struct.unpack_from("<4H", raw_spr, 776 + index * 8)
    assert output["flags"] == flags and output["cell_count"] == cells == 69
    assert output["payload_size"] == payload
    assert output["cells"] == [dict(index=index, size=[w,h], displacement=[x,y])]
    assert bytes(v for rgb in output["palette"] for v in rgb) == raw_spr[8:776]
for args in (("--label", "missing", fin), ("--frame", -1, fin), ("--frame", frames, fin),
             ("--cell", 69, spr), ("--frame", 0, spr), ("--cell", 0, fin)):
    assert subprocess.run([TOOL, *map(str,args)], capture_output=True).returncode != 0
with tempfile.TemporaryDirectory() as tmp:
    bad = Path(tmp) / "bad.FIN"
    for data in (raw[:7], raw[:co], raw[:-1]):
        bad.write_bytes(data)
        assert subprocess.run([TOOL, str(bad)], capture_output=True).returncode != 0
    bad_range = bytearray(raw)
    struct.pack_into("<H", bad_range, lo + 18, frames)
    bad.write_bytes(bad_range)
    assert subprocess.run([TOOL, str(bad)], capture_output=True).returncode != 0
    bad = Path(tmp) / "bad.SPR"
    bad.write_bytes(raw_spr[:778])
    assert subprocess.run([TOOL, str(bad)], capture_output=True).returncode != 0
subprocess.run(["python3", "tools/dc_states.py", "--check"], check=True)
assert Path("games/dark-colony/blood_labels.inc").read_text().count('{ "') == 208
print("PASS: FIN/SPR JSON matches native records, invalid inputs fail, state export reproduces exactly")
