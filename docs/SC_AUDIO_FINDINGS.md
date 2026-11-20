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
were removed. SoundPriority now goes to its own field,
`sfxinfo_t.pool_priority`.

Confirmed from PyMS: a sound with a numerically higher SoundPriority replaces
a lower one. PyMS does not say which of several lower channels is replaced,
how ties resolve, or what a full set of never-preempt sounds does. OpenBW's
UI settles all three, and the engine follows it. See "Channel priority and
never_preempt" below. The engine's 32 mixer channels still exist; StarCraft's
pooled sounds are limited to 8 of them.

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
remain unaffected. This local flag persists in engine save data. At the time
this audio finding was recorded, the save layout was 15; that version number
is historical. Later StarCraft state advanced the current save layout to 17;
see [SC_CONSTRUCTION_FINDINGS.md](SC_CONSTRUCTION_FINDINGS.md). The mute flag
is intentionally excluded from the consistency hash, and
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

## Script-timed sounds (implemented)

The IScript runtime in `games/starcraft/p_iscript.c` (see
[SC_PROJECTILE_VISUALS.md](SC_PROJECTILE_VISUALS.md)) now plays sounds from the
script frame that carries them, with OpenBW's `playsnd`, `playsndrand` and
`playsndbtwn` semantics: a fixed row, `ids[r % n]` from `lcg_rand` source 4,
or `a + r % (b - a + 1)` from source 5. The draws come from `sc_random`, the
synchronized LCG OpenBW uses for them, and are made whether or not the sound
is audible. Wired animations: missile Death (impact) and bounce
(`SpecialState1`), Burrow, UnBurrow, Lift Off and Landing. Rows 471/472 are the
Terran lift-off (frame 0) and landing (frames 15 to 25) sounds, 354 a Spider
Mine's burrow, 91 and 93 a Glave Wurm's bounce and impact, and 72, 79, 9, 66,
614 and others the impact sounds of the other bullets. Sound rows named by
these scripts are loaded with the bank (`mark_rows` in `sounds.c`). A script
sound honors Mute Unit Speech per chosen row (choose, then suppress) and the
fog rule of `S_ActorSound`, and goes through `sc_script_play` so tests can
observe it. Attack and death sounds still use the first-sound lookup at the
engine's attack and death events.

## Channel priority and never_preempt (OpenBW policy, implemented)

- `sfxdata.dat` has 968 rows here: SoundPriority (the second column) is 0 for
  12 rows, 1 for 825, 2 for 130 and 3 for one; `never_preempt` (flag bit 5,
  PyMS `Sound.Flag.never_preempt`) is set on 753 rows, `unit_speech` on 697,
  `one_at_a_time` on 27 (counted from the installed file).
- An earlier note here said OpenBW has no policy. That was wrong: the game
  core's `state_functions::play_sound` is an empty hook, but OpenBW's UI
  overrides it. Pinned `ui/ui.h` (`4b046d5f`, `play_sound` and
  `get_sound_channel`) runs sfxdata sounds on 8 channels:
  1. A `one_at_a_time` (0x10) sound is dropped while the same row plays.
     Otherwise a `unit_speech` (0x02) sound with a source unit is dropped
     while a speech sound from a unit of the same type plays.
  2. A free channel is taken if there is one.
  3. Otherwise the victim is the first channel, in channel order, with the
     lowest priority strictly below the new sound's. A playing
     `never_preempt` (0x20) sound is never a victim; the new sound's own
     flag does not matter. Equal priority never preempts.
  4. With no victim the new sound is dropped.
- Implemented as an opt-in pool in the shared sound layer (`sound/s_sound.c`
  `make_room`; `sfxinfo_t.pooled`, `pool_priority`, `never_preempt`,
  `speech`; `soundinfo_t.pool_channels`). StarCraft pools every sfxdata row
  on 8 channels. The briefing and trigger WAVs, the glue click and music are
  added outside the pool, so they stay independent, and so does the speech
  mute. `sfxinfo_t.priority` (Dark Colony's bark priority, lower wins) is
  untouched. Other games pool nothing. `tests/shared/test_sound.c::test_pool`
  covers the free channel, the first lowest victim, equal priority,
  `never_preempt`, same-type speech, and unpooled sounds not counting.
- Not adopted from OpenBW's UI: its 80 ms wall-clock repeat throttle per row
  and its `min_volume`/screen-distance volume, which this engine's own
  attenuation replaces.
- Caveat: OpenBW's UI is a reimplementation, not a STARCRAFT.EXE trace.
  Retail's channel count and tie order are as OpenBW has them unless a trace
  of the executable's sound-start routine says otherwise.

## Remaining gaps outside this package

Several broader audio omissions remain documented in
`docs/SC_EXE_FINDINGS.md` and require additional source work:

- Spell sounds and the attack and death sounds are not yet driven by their
  scripts; only the animations listed above are.
- Arbitrary scripts' idle loops are not played, so their random draws are not
  made (retail draws them).
- Sfxdata minimum-volume behavior, positional falloff, music sequence fidelity,
  and exact retail option semantics remain outside this investigation.

Stargus references used for the gap review include
`reference/stargus/doc/iscript.txt` (for example, Siege Tank scripts reference
sound rows 317–319 and Terran lift/land scripts rows 471–472) and
`reference/stargus/scripts/sound.lua` (race playlists and button/placement
sound definitions). These provide implementation leads only; they do not
prove retail timing or complete behavior.
