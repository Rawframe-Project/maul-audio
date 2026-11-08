#!/bin/sh
# Runs a test application in the Android emulator: installs it on the
# device ANDROID_SERIAL names (else the only one), starts its activity,
# and waits, two minutes at most from the start, for the closing line
# the test writes to files/out, "result: N failures", read with run-as
# (the application is debuggable). A line "adb: <command>" the test writes is run once,
# as "adb shell <command>", so that the test drives what only the
# system can do (granting a permission). Passes when the line says 0
# failures; otherwise shows the application's crashes from the log. The
# application is uninstalled at the end.
#
# adb can lose its connection to the device ("error: closed"). The steps
# before the verdict (install, clearing the log, the start, a command the
# test asks for) are idempotent, so each is tried up to three times. Each
# run starts with the application fresh: one an earlier run left, as
# when its uninstall was lost, is removed with its data before the
# install. The system handles a package's removal in its own time, from
# its broadcasts, and a handler that gets to one after the test started
# acts on the new run's data (the media provider orphans the documents
# the package owns), so the runner waits, a minute at most, until the
# broadcasts are delivered. Should the removal be lost too, install -r
# keeps the data, so files/out is removed as well, and the device must
# answer that it is gone before the start. Each answer is read whole
# before its carriage returns are stripped: through a pipe, a read adb
# lost would pass as an empty answer. Stopping and uninstalling it after
# the verdict cannot change the verdict.
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

# An idempotent adb step's output, tried up to three times while adb
# fails: only the try that succeeded is kept, so an answer a lost try
# printed before its connection dropped is not given twice.
ask() {
    tries=1
    until output=$("$adb" "$@"); do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
    printf '%s\n' "$output"
}

if ! installed=$(ask shell pm list packages "$package"); then
    echo "run_android_app.sh: adb lost whether $package is installed" >&2
    exit 1
fi
if printf '%s\n' "$installed" | tr -d '\r' | grep -qx "package:$package"; then
    again uninstall "$package" > /dev/null
fi
again install -r "$apk" > /dev/null
if ! timeout 60 "$adb" shell am wait-for-broadcast-idle > /dev/null; then
    echo "run_android_app.sh: the system's broadcasts were not seen idle in 60 s"
fi
again shell run-as "$package" rm -f files/out
# absent only from a test that ran: a run-as that fails answers nothing.
if left=$(ask shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'"); then
    left=$(printf '%s' "$left" | tr -d '\r')
else
    left=lost
fi
if [ "$left" != absent ]; then
    echo "run_android_app.sh: an earlier run's files/out may still be there (the device answered '$left')" >&2
    again uninstall "$package" > /dev/null || true
    exit 1
fi
again logcat -c
start=$(date +%s)
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
