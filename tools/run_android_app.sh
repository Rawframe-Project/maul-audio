#!/bin/sh
# Runs a test application in the Android emulator: installs it on the
# device ANDROID_SERIAL names (else the only one), starts its activity,
# and waits, two minutes at most, for the closing line the test writes
# to files/out, "result: N failures", read with run-as (the application
# is debuggable). A line "adb: <command>" the test writes is run once,
# as "adb shell <command>", so that the test drives what only the
# system can do (granting a permission). Passes when the line says 0
# failures; otherwise shows the application's crashes from the log. The
# application is uninstalled at the end.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
start=$(date +%s)
"$adb" install -r "$apk" > /dev/null
"$adb" logcat -c
"$adb" shell am start -W -n "$package/android.app.NativeActivity" > /dev/null
out=$(mktemp)
done=0
while [ $(($(date +%s) - start)) -lt 120 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    commands=$(grep -E '^adb: ' "$out" || true)
    count=$(printf '%s' "$commands" | grep -c '' || true)
    while [ "$done" -lt "$count" ]; do
        done=$((done + 1))
        line=$(printf '%s\n' "$commands" | sed -n "${done}p")
        echo "running: $line"
        "$adb" shell "${line#adb: }" > /dev/null
    done
    grep -q '^result: ' "$out" && break
    sleep 0.5
done
grep -v -E '^adb: ' "$out" || true
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F | tail -60
fi
"$adb" shell am force-stop "$package"
"$adb" uninstall "$package" > /dev/null
rm -f "$out"
exit $status
