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
