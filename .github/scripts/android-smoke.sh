#!/bin/bash
# Invoked inside the emulator runner, once per game APK.
set -euo pipefail
package=$1
: > android-smoke.log
trap 'adb logcat -d >> android-smoke.log || true' EXIT
adb logcat -c
marker() { adb logcat -d -s SDL SDL/APP | grep -F "OPEN_RTS_SMOKE_$1" >/dev/null; }
probe() {
  adb shell am start -W -n "$package/org.openrts.SmokeActivity"
  for attempt in {1..60}; do
    if marker READY; then return; fi
    sleep 1
  done
  adb logcat -d
  return 1
}
probe
# Native initialization can precede Android's first window focus callback.
sleep 1
adb shell input tap 150 150
adb shell input keyevent KEYCODE_HOME
sleep 2
adb shell am start -n "$package/org.openrts.LauncherActivity"
adb shell am start -W -n "$package/org.openrts.SmokeActivity"
# Bringing the existing SDL Activity back exercises its surface and resume path.
for attempt in {1..60}; do
  if marker OK; then break; fi
  sleep 1
done
adb logcat -d > android-smoke.log
for name in READY TOUCH BACKGROUND FOREGROUND OK; do
  grep -q "OPEN_RTS_SMOKE_$name" android-smoke.log
done
if grep -E 'FATAL EXCEPTION|Fatal signal|ANR in' android-smoke.log; then exit 1; fi
# Wait for Activity destruction to finish before starting the next fresh process.
for attempt in {1..30}; do
  if ! adb shell pidof "$package:game" >/dev/null; then break; fi
  sleep 1
done
if adb shell pidof "$package:game" >/dev/null; then exit 1; fi
adb logcat -c
probe
for attempt in {1..60}; do
  if marker OK; then break; fi
  sleep 1
done
marker OK
adb logcat -d >> android-smoke.log
if grep -E 'FATAL EXCEPTION|Fatal signal|ANR in' android-smoke.log; then exit 1; fi
