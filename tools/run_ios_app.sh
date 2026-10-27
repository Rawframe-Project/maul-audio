#!/bin/sh
# Runs a test application in the iOS simulator: grants it the
# microphone, installs the bundle on the booted device (or the one
# MAUD_IOS_DEVICE names), launches it with its output in a file, and
# waits, two minutes at most, for the closing line the test prints,
# "result: N failures". simctl launch returns once the application runs,
# not when it ends, so the runner ends it. Passes when the line says 0
# failures; otherwise shows the application's errors and, if it ended,
# its crash report.
#
# Installing and launching have a bound of their own, five minutes, apart
# from the application's two: a simulator that is slow to take the bundle
# fails as that, naming it, and never eats into the test's time. A
# loaded runner has taken 194 s to launch where it usually takes 30.
set -eu
launch_limit=300
run_limit=120

# Runs a command in the background with its output in a file, ending it
# past a bound in seconds; fails, naming what took too long.
bounded() { # seconds, what, output file, command...
    limit=$1
    what=$2
    output=$3
    shift 3
    "$@" >"$output" &
    child=$!
    waited=0
    while kill -0 "$child" 2>/dev/null; do
        if [ "$waited" -ge "$limit" ]; then
            # The command's own processes first: they are this run's.
            pkill -P "$child" 2>/dev/null || true
            kill "$child" 2>/dev/null || true
            echo "$what took more than $limit s"
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    wait "$child"
}

app=$1
device=${MAUD_IOS_DEVICE:-booted}
name=$(basename "$app" .app)
id=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$app/Info.plist")
out=$(mktemp -t maud-ios-out)
err=$(mktemp -t maud-ios-err)
said=$(mktemp -t maud-ios-launch)
start=$(date +%s)
# simctl says "<id>: <pid>"; the simulator's processes are the host's.
# The application writes its output where MAUD_TEST_OUT names, simctl's
# own redirection giving nothing on a CI runner.
# Chained, since set -e does not reach a function run as a condition.
launch() {
    xcrun simctl install "$device" "$app" &&
        xcrun simctl privacy "$device" grant microphone "$id" &&
        SIMCTL_CHILD_MAUD_TEST_OUT="$out" xcrun simctl launch --terminate-running-process \
            --stderr="$err" "$device" "$id"
}
if ! bounded "$launch_limit" "installing and launching $id" "$said" launch; then
    xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
    xcrun simctl uninstall "$device" "$id" >/dev/null 2>&1 || true
    rm -f "$out" "$err" "$said"
    exit 1
fi
pid=$(sed 's/.*: //' "$said")
echo "launched $id as $pid in $(($(date +%s) - start)) s"
launched=$(date +%s)
while [ $(($(date +%s) - launched)) -lt "$run_limit" ] && ! grep -q '^result: ' "$out"; do
    sleep 1
done
cat "$out"
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    cat "$err"
    if ! kill -0 "$pid" 2>/dev/null; then
        report=$(ls -t "$HOME"/Library/Logs/DiagnosticReports/"$name"* 2>/dev/null | head -1)
        if [ -n "$report" ]; then
            head -c 2000 "$report"
            echo
            # The report is a header line and a JSON body; the faulting
            # thread's frames and the abort's message lie past any head.
            python3 - "$report" <<'PY' || true
import json, sys
body = json.loads(open(sys.argv[1]).read().split("\n", 1)[1])
print("abort:", json.dumps(body.get("asi", {})))
images = body.get("usedImages", [])
thread = body["threads"][body["faultingThread"]]
print("faulting thread", body["faultingThread"], thread.get("name", ""), thread.get("queue", ""))
for frame in thread.get("frames", [])[:40]:
    index = frame.get("imageIndex", -1)
    image = images[index].get("name", "?") if 0 <= index < len(images) else "?"
    print(" ", image, frame.get("symbol", "?"), frame.get("symbolLocation", ""),
          frame.get("sourceFile", ""), frame.get("sourceLine", ""))
PY
        fi
    fi
fi
xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
xcrun simctl uninstall "$device" "$id" >/dev/null 2>&1 || true
rm -f "$out" "$err" "$said"
exit $status
