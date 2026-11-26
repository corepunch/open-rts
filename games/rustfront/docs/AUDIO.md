# Rustfront audio

Every sound in Rustfront is synthesised by one script, `art/sfx.py`, from oscillators,
filtered noise, envelopes and a few effects. No sample, loop or instrument comes from
anywhere else. The script is pure Python 3 and uses only the standard library (`wave`,
`math`, `random`, `zlib`); it needs no numpy or Pillow.

```sh
python3 games/rustfront/art/sfx.py                    # everything, about a minute
python3 games/rustfront/art/sfx.py w_rifle1 mus_menu  # just these
python3 games/rustfront/art/sfx.py --list             # every name
make rustfront-audio                                  # the script, then relink the game
```

Each sound's random numbers come from a generator seeded with the CRC-32 of its name, so a
run reproduces the committed files byte for byte. Variants such as `w_rifle1` and
`w_rifle2` share a recipe and differ only by that seed. The generated WAVs are committed,
so the game builds and plays without running the script. `art/build_all.sh` runs it as
part of a full art rebuild.

## Files

| Where | Rate | What |
|---|---|---|
| `assets/sfx/*.wav` | 22050 Hz, 16-bit mono | effects, voices, announcements; the base hum loop |
| `assets/sfx/amb_wind.wav` | 11025 Hz | the wind loop (it holds nothing above 2 kHz) |
| `assets/music/*.wav` | 16000 Hz, 16-bit mono | the four music loops |

About 2.4 MB of effects and 4.6 MB of music. The engine's mixer (`sound/i_sound.c`)
converts every sample to its own 22050 Hz output when it loads it.

## The sounds

**Weapons** (`w_*`). A rifle round is a high-passed noise crack, a band-passed body, a
short falling sine thump and a low slap off the ground. The machine gun fires five such
rounds 72 ms apart. The cannon and the artillery add a deep falling sine and a noise
cloud whose cutoff falls; the artillery's rumble runs on. A rocket is an ignition pop,
then a band-passed hiss whose centre climbs. The flamethrower is low-passed noise with a
flicker and crackle. The Concord's beam is an FM chirp falling from 3.2 kHz with a glassy
echo; the heavy beam charges up, then discharges through three detuned saws.

**Impacts** (`hit_*`, `boom_*`, `zap*`, `flame_hit`, `collapse`). Explosions share one
recipe with a size: a crack, a falling body, a darkening noise cloud and debris crackle.
A structure's collapse layers several blasts, a groaning girder (narrow band-pass noise
sliding down), falling rubble and settling dust. A bullet hit is a tick, with a
ricochet's whine in some variants.

**Construction**. `build_place` is a heavy clunk with ringing steel, as the foundation
drops. `build_scaffold` is hammer strikes, a ratchet and a pneumatic hiss; a site plays it
about every 1.7 s while it rises. `build_done` is hydraulics, a clunk and a rising chime.

**Announcements** (`ui_*`). These are computer chimes, not words: FM bells for a unit or
a structure ready, a falling saw that dies twice for low power, two low buzzes for
insufficient funds, an alternating two-tone klaxon for under attack, and a short tick for
a click.

**Voices** (`dir_*`, `cl_*`, `lc_*`; select, move and attack, two variants each). These
are wordless, but each one has the intonation of a reply: a selection asks (rising), a
move order confirms (falling), an attack order barks (fast and accented).
- **Directorate:** military radio. Formant-filtered sawtooth syllables gliding between
  vowels, band-limited to a telephone band, clipped, with static, a squelch at each end
  and a confirmation pip.
- **Scrap Clans:** a gritty loudhailer. A lower, growling voice, amplitude-modulated at
  about 30 Hz, overdriven and crushed to 6 bits at half rate.
- **Lumen Concord:** crystalline. FM chimes on a bright D pentatonic that glide into each
  other, with a breathy formant shimmer and a glassy echo.

**Ambience**. `amb_wind` is a 14 s loop of low-passed noise with slow gusts and a faint
whistle. `amb_base` is a 6 s loop of mains hum, rumble and a pump's thud; it plays from the
player's headquarters, so it fades as the view moves away. Both loops are seamless: the
noise is rendered at twice the length and its end cross-faded into what led up to its
start, and every tone completes whole cycles in the loop.

**Music** (`mus_*`). The music comes from a small tracker in the script: 16th-note step
patterns placed into a buffer the length of the loop. Whatever rings past the end wraps
into the start, and the reverb and the master filter run twice round, so each track loops
without a seam. The instruments are synthesised: a sine-sweep kick, noise snares and
hats, toms, metal clanks, a filtered saw bass, detuned-saw brass and pads, FM bells, a
Karplus-Strong string, and overdriven power chords.

| Track | Key, tempo | Character |
|---|---|---|
| `mus_menu` | D minor, 84 | cold pad, falling bell arpeggios, a distant war drum, a muted horn theme in the second half |
| `mus_directorate` | C minor, 112 | a march: snare rolls, a driving bass ostinato, brass theme, call and answer |
| `mus_clans` | E phrygian, 138 | distorted palm-muted power chords, pounding kick and toms, scrap-metal clanks, a crushed snarling lead |
| `mus_concord` | A lydian, 92 | interlocking bell arpeggios, a wide airy pad, a soft pulse, a long reverb and a slow bell melody |

## How the game plays them

`s_rustfront.c` defines `rf_soundinfo`, which `info.c` hangs on `gameinfo_t.sound`. Its
table (`SOUNDS`) gives each sound its files, volume in centibels, how many may play at
once, and whether it loops or counts as music. At `S_Init` each file becomes an engine
sfx, and a sound with variants becomes a group over them, played at random and never
twice running.

| Event | Source | Sound |
|---|---|---|
| `SE_ATTACK` | the engine, when a unit fires | the shooter's weapon (`WEAPONS` table), positional |
| `SE_EXPLODE` | a projectile landing | its impact (`IMPACTS` table), positional |
| `SE_DEATH` | a unit dying | a soldier's cry, a vehicle's explosion, or a structure's collapse, positional |
| `G_UnitDamaged` | a machine-gun hit (`g_mission.c`) | a bullet hit on the victim, positional |
| `SE_SELECT`, `SE_ACK`, `SE_ACK_ATTACK` | the player selecting or ordering | the faction's voice; a structure answers a click with a click |
| `SE_READY` | production finishing | unit ready or structure ready |
| `SE_ATTACKED` | the player's unit taking damage | under attack, at most once per 20 s of game time |
| `SE_NO_FUNDS` | clicking a build row the player cannot afford | insufficient funds, at most once per 2 s |
| `G_ModelPlaceConstruction`, site completion | `g_build.c` | placing and finishing, positional |
| the sound ticker | every frame | music, wind, the headquarters' hum, scaffolding |

**Positional sound.** The engine attenuates by the squared distance from the centre of
the view: `rolloff` 8 cB per square cell gives about -8 dB ten cells out, and nothing
plays past about 27 cells (`cutoff` 6000). Pan follows the horizontal offset.

**Music.** The front end plays `mus_menu`. In a game the ticker looks up the player's
faction once a second (`RF_OwnerFaction`) and plays that faction's track. A loop that
stops, for example when `S_Start` silences everything for a new level, starts again on
the next frame. Music uses the engine's separate music volume (`snd_music_volume`,
`S_SetMusicVolume`), so a menu slider can set it on its own.

**Other code** can play a sound with `RF_PlaySound(sound, origin)`: pass `NULL` for a
local one. `RF_Alert(sound)` plays an announcement through the same rate limit. The
low-power warning (`RF_SND_LOW_POWER`) is ready for a power rule to call; nothing raises
it yet.

**Engine additions.**
- `SE_ACK_ATTACK` (`game/g_cmd.c`): an attack order's bark. A game without an answer of
  its own gets its `SE_ACK` one (`S_Bark`).
- `SE_NO_FUNDS` (`hud/hu_bar.c`): a click on an available product its owner cannot
  afford.

Neither changes any other game.

## Adding a sound

1. Write a recipe in `art/sfx.py`: a function `rng -> list of floats`. Add it to `SFX`
   with a peak level, or to `LOOPS` or `MUSIC`.
2. Run `python3 games/rustfront/art/sfx.py NAME`.
3. Add an `RF_SND_*` to `rustfront.h` and its row to `SOUNDS` in `s_rustfront.c`. Then map
   it to an event, or call `RF_PlaySound` with it.
4. `make test-rustfront`: `tests/rustfront/test_sound.c` checks that every registered
   sound and variant loads and plays, and that the events map as described above.
