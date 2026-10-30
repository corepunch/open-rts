# Android review and release validation

## Confirmed by automated checks

`GameDataTest` covers all seven startup manifests, preservation of case and the
selected data tree, missing/empty prerequisites, invalid ZIPs, truncated central
directories, CRC corruption, traversal/absolute/Windows paths, duplicate entries,
file/directory collisions, excessive depth, compressed/expanded/per-file/entry
limits, provider failure, replacement rollback and recovery after interrupted
commit. Fixtures are synthetic; they do not establish retail-file compatibility.

The importer spools at most 8 GiB of compressed input into private files storage,
then extracts at most 16 GiB, at most 2 GiB per file and 200,000 entries. It keeps
16 MiB free during writes. Storage must hold the compressed archive, extracted
files and any previous installation at the same time. Failed imports preserve
the previous installation. Reimport replaces the whole game directory, so saves
should remain in the engine's user directory, outside retail data.

The readiness receipt is written only after CRC and startup-prerequisite checks.
The launcher rechecks those files before Play. Older imports without a receipt
must be reimported. The prerequisite lists follow the C loaders' case-sensitive
paths; they are minimum startup checks, not a complete retail inventory or
validation of proprietary archive contents. A ZIP can still contain malformed
native files or omit later missions/art/audio. The native loaders remain
responsible for interpreting their formats.

Android CI builds/lints/tests all seven games for ARM64 and x86-64, checks APK
signatures, 16 KiB ZIP alignment and every packaged ELF LOAD segment, licenses
and required libraries. The new emulator job uses API 35/x86-64 debug APKs. Its
asset-free probe enters through SDL's real Java/JNI SDL_main path, creates the
engine SDL renderer, verifies synthesized touch mouse events, Home/resume and
a second launch after native shutdown. Probe entry points are absent from
release Java code. Logs are retained as workflow artifacts. Consult the actual
workflow conclusion before claiming the emulator checks have passed.

## Lifecycle and input

The first emulator run found a native entry-point stack overflow: SDL_main's
frame was 1,144,752 bytes, including a 1,114,160-byte sprite cache, while SDL's
Java-created thread has a roughly 1 MiB stack. Moving that game-lifetime cache
to static storage reduces the measured frame to 30,592 bytes. Android builds
treat individual frames over 256 KiB as errors to prevent this regression.

SDLActivity retains ownership of its surface, audio, pause/resume and native
thread lifecycle. The engine suppresses presentation between background and
foreground events and recreates its streaming texture on SDL_RENDER_DEVICE_RESET
from the indexed framebuffer. A headless pixel regression verifies these paths.
GameActivity runs in `:game` and terminates that process after
SDL shutdown, because plugins use C globals and atexit handlers that cannot be
safely reused by calling main again. The launcher/import live in the main
process. Import workers retain only the application context and survive Activity
recreation; resumed launchers observe process-owned progress and disable Play
while importing. Force-stop or process death cancels the operation; staging is
removed on the next import and the prior installation is recoverable.

SDL2 supplies single-finger left-button/drag events in window coordinates. Right
click, keyboard shortcuts and precise camera controls require external input;
this is not a complete touch-only control scheme. The smoke probe checks the
SDL input path, not mission selection, unit commands or HUD coordinate accuracy.

## Signing and publishing

The release workflow builds the selected tag and attaches exactly seven named
APKs using the persistent Actions keystore. Missing signing secrets fail the
build. PR artifacts use the debug key; release artifacts use the configured
release key. No signing key is committed. Version codes use seconds since
2020-01-01 from the source commit timestamp, shared across workflows; older
source revisions cannot replace newer installed revisions, and rebuilding the
same commit retains its code. Keep the keystore and package IDs stable.

This review does not expose or independently verify private keystore contents.
Actual release-key fingerprint continuity and installation over a previously
published APK must be checked on the release candidate. No release is created
by merging the Android PR. Publishing requires a separate release decision.

## Still required before a public release

- Run each game with legally supplied retail data: main menu, first mission,
  later mission/resource transitions, sound, music and video where supported.
- Check real game touch coordinates, selection/dragging and external mouse,
  keyboard and right-button orders on ARM64 hardware.
- Check mission pause/resume, screen lock, audio focus, save/load, Back/quit,
  relaunch and process death, including interruption of a large import.
- Exercise Android 6/API 23 and a 16 KiB-page ARM64 device/emulator. API 35/x86-64
  smoke alone does not establish these compatibility claims.
- Test a real multi-GiB retail ZIP and low-storage failure on a device. Automated
  small-limit tests validate bounds and rollback, not device I/O throughput.
- Verify signed release APKs against the expected certificate fingerprint and
  update an existing installation; publish only after release approval.
