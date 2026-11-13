# StarCraft audio follow-up

## Rejected interpretation: SoundPriority is not bark priority

`arr/sfxdata.dat` stores 9-byte rows in column-major order. The local PyMS
format definition at `reference/PyMS/PyMS/FileFormats/DAT/SoundsDAT.py` lists
`sound_file`, `priority`, `flags`, `portrait_length_adjust`, and
`minimum_volume`; priority is the second column and flags the third. PyMS's
`reference/PyMS/PyMS/Data/Hints.txt:482` describes SoundPriority specifically
as allowing a higher-priority sound to override a lower-priority sound already
playing when StarCraft lacks free sound channels. PyMS defines the separate
`never_preempt` flag as bit 5 in the flags column.

An initial change incorrectly copied this value into `sfxinfo_t.priority`.
That field is used by shared `sound/s_sound.c::S_Bark` to pick the lowest
numeric bark priority, a distinct Dark Colony rule. That change and its test
were removed. The StarCraft loader currently reads the one-at-a-time flag but
does not import SoundPriority or never-preempt behavior.

Confirmed from PyMS: the direction of override is toward the numerically
higher SoundPriority. The documented source does not settle how StarCraft
chooses among several eligible channels to interrupt, how ties are resolved,
whether never-preempt sounds can themselves interrupt lower priorities, or
what the exact behavior is when every channel is non-preemptible. The engine's
32 mixer channels are an implementation detail, not evidence for retail's
channel count. Therefore channel arbitration is not implemented from this
evidence alone. It needs additional authoritative behavior evidence and a
focused channel-exhaustion regression before a parity claim is appropriate.

## Mute Unit Speech trigger actions

PyMS defines `Sound.Flag.unit_speech` as bit 1 (`0x02`) in the flags column of
`sfxdata.dat` (`reference/PyMS/PyMS/FileFormats/DAT/SoundsDAT.py:14-20`). Its
hint at `reference/PyMS/PyMS/Data/Hints.txt:490` explicitly says sounds carrying
this property are suppressed by the Mute Unit Speech trigger action. The
StarCraft loader now retains this bit per native SFX row. TRIG action 30 sets
and action 31 clears a mission-owned, local presentation flag, gated to the
trigger's current player matching `consoleplayer`; `sc_actor_sound` suppresses
flagged rows. For synthetic ranged SFX groups it builds/reuses a group of the
unflagged members. This is an engine approximation: it filters before the
random group member is selected, so a mixed group's probabilities differ from
selecting first and then suppressing a flagged sample. PyMS documents the
flag's suppression effect but does not establish when retail applies it
relative to group selection. Actor responses, barks, and other sounds routed
through the actor callback follow the native row metadata, while unflagged
callbacks remain audible.

The trigger's WAV actions 7/8 use `sc_brief_voice` and a dedicated briefing
sound slot, outside the SFX-row callback, so mute does not suppress scenario
audio. Ordinary UI/placement sounds also use their own direct sound calls and
remain unaffected. This local flag persists in engine save data; the save
layout is 15. It is intentionally excluded from the consistency hash, and
resync keeps the receiving peer's value instead of importing the sender's
local presentation setting.

Evidence confirms the per-row flag and that Mute Unit Speech suppresses such
rows. Native mixed-group selection order remains unknown; no retail trace has
verified whether the flag is checked before or after random member selection.
The repository does not model the separate Unit Speech, Unit
Acknowledgements, or Building Sounds user preferences mentioned by the same
PyMS hint, nor does it reproduce retail portrait-length adjustment. Trigger
ownership/local-player presentation is the implementation boundary; this is
not a claim that every retail audio preference has been reproduced.

Focused regression coverage is in `tests/starcraft/test_sounds.c` (TRIG 30/31,
grouped Marine SELECT/ACK playback, unflagged SCV attack group playback, a
synthetic mixed group, a nonlocal trigger owner, ordinary placement sound, and
WAV actions while muted)
and `tests/starcraft/test_save.c` (ordinary save/load, peer-local resync, hash
exclusion, and save-layout version).

## Confirmed gaps outside this package

Several broader audio omissions remain documented in
`docs/SC_EXE_FINDINGS.md` and require additional source work:

- Missile impact sounds require mapping weapon/projectile identity through the
  current combat lifecycle (`p_combat.c`) and/or compiling more IScript events
  (`w_assets.c`).
- Lift, land, burrow and spell sounds need sound events tied to script/state
  timing. The current actor callback only receives coarse engine events; a
  correct implementation needs the IScript/action path rather than guessing
  timing from the order request.
- Arbitrary `playsnd`, `playsndrand`, and `playsndbtwn` playback remains outside
  the limited first-sound lookup. Completing this requires a broader script
  runtime/compiler change, not a static WAV mapping.
- Sfxdata minimum-volume behavior, positional falloff, music sequence fidelity,
  and exact retail option semantics remain outside this investigation.

Stargus references used for the gap review include
`reference/stargus/doc/iscript.txt` (for example, Siege Tank scripts reference
sound rows 317–319 and Terran lift/land scripts rows 471–472) and
`reference/stargus/scripts/sound.lua` (race playlists and button/placement
sound definitions). These provide implementation leads only; they do not
prove retail timing or complete behavior.
