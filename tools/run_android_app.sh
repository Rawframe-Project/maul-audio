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
#
# adb can lose its connection to the device ("error: closed"). The steps
# before the verdict (install, clearing the log, the start, a command the
# test asks for) are idempotent, so each is tried up to three times. The
# application's data is cleared after the install, since install -r
# keeps it: a files/out an earlier run left (its uninstall lost) would
# otherwise be read as this run's result. pm clear can fail and still
# exit 0 on some Android versions, so files/out is removed as well, and
# the device must answer that it is gone before the start. Stopping and
# uninstalling it after the verdict cannot change the verdict.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2

# An idempotent adb step, tried up to three times while adb fails.
again() {
    tries=1
    until "$adb" "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

start=$(date +%s)
again install -r "$apk" > /dev/null
again shell pm clear "$package" > /dev/null
again shell run-as "$package" rm -f files/out
# absent only from a test that ran: a run-as that fails answers nothing.
left=$(again shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'" |
    tr -d '\r')
if [ "$left" != absent ]; then
    echo "run_android_app.sh: an earlier run's files/out is still there ($left)" >&2
    again uninstall "$package" > /dev/null || true
    exit 1
fi
again logcat -c
again shell am start -W -n "$package/android.app.NativeActivity" > /dev/null
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
        again shell "${line#adb: }" > /dev/null
    done
    grep -q '^result: ' "$out" && break
    sleep 0.5
done
grep -v -E '^adb: ' "$out" || true
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F | tail -60 || true
fi
again shell am force-stop "$package" || true
again uninstall "$package" > /dev/null || true
rm -f "$out"
exit $status
