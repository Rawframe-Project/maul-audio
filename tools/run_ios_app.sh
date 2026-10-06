#!/bin/sh
# Runs a test application in the iOS simulator: grants it the
# microphone, installs the bundle on the booted device (or the one
# MAUD_IOS_DEVICE names), launches it with its output in a file, and
# waits, two minutes at most, for the closing line the test prints,
# "result: N failures". simctl launch returns once the application runs,
# not when it ends, so the runner ends it. Passes when the line says 0
# failures; otherwise shows the application's errors and, if it ended,
# its crash report.
set -eu
app=$1
device=${MAUD_IOS_DEVICE:-booted}
name=$(basename "$app" .app)
id=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$app/Info.plist")
out=$(mktemp -t maud-ios-out)
err=$(mktemp -t maud-ios-err)
start=$(date +%s)
xcrun simctl install "$device" "$app"
xcrun simctl privacy "$device" grant microphone "$id"
# simctl says "<id>: <pid>"; the simulator's processes are the host's.
# The application writes its output where MAUD_TEST_OUT names, simctl's
# own redirection giving nothing on a CI runner.
pid=$(SIMCTL_CHILD_MAUD_TEST_OUT="$out" xcrun simctl launch --terminate-running-process \
    --stderr="$err" "$device" "$id" | sed 's/.*: //')
echo "launched $id as $pid in $(($(date +%s) - start)) s"
launched=$(date +%s)
while [ $(($(date +%s) - launched)) -lt 120 ] && ! grep -q '^result: ' "$out"; do
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
        [ -n "$report" ] && head -c 6000 "$report"
    fi
fi
xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
xcrun simctl uninstall "$device" "$id" >/dev/null 2>&1 || true
rm -f "$out" "$err"
exit $status
