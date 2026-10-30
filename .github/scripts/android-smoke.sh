#!/bin/bash
# Invoked inside the emulator runner, once per game APK.
set -euo pipefail
package=$1
adb logcat -c
probe() {
  adb shell am start -W -n "$package/org.openrts.SmokeActivity"
  for attempt in {1..60}; do
    if adb logcat -d -s SDL | grep -q OPEN_RTS_SMOKE_READY; then return; fi
    sleep 1
  done
  adb logcat -d
  return 1
}
probe
adb shell input tap 150 150
adb shell input keyevent KEYCODE_HOME
sleep 2
adb shell am start -n "$package/org.openrts.LauncherActivity"
adb shell am start -W -n "$package/org.openrts.SmokeActivity"
# Bringing the existing SDL Activity back exercises its surface and resume path.
for attempt in {1..60}; do
  if adb logcat -d -s SDL | grep -q OPEN_RTS_SMOKE_OK; then break; fi
  sleep 1
done
adb logcat -d > android-smoke.log
for marker in READY TOUCH BACKGROUND FOREGROUND OK; do
  grep -q "OPEN_RTS_SMOKE_$marker" android-smoke.log
done
if grep -E 'FATAL EXCEPTION|Fatal signal|ANR in' android-smoke.log; then exit 1; fi
# A second launch must create a fresh C runtime, with no stale globals/atexit.
adb logcat -c
probe
for attempt in {1..60}; do
  if adb logcat -d -s SDL | grep -q OPEN_RTS_SMOKE_OK; then break; fi
  sleep 1
done
adb logcat -d >> android-smoke.log
adb logcat -d -s SDL | grep -q OPEN_RTS_SMOKE_OK
