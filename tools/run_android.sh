#!/bin/sh
# Runs a test executable in the Android emulator, as CTest's
# cross-compiling emulator (cmake/android-emulator.cmake):
#
#   tools/run_android.sh <executable> [arguments...]
#
# Pushes it to a directory of its own under /data/local/tmp/maul-audio/
# on the device ANDROID_SERIAL names (else the only one), runs it there
# with the environment's MAUD_ variables and exits with its status.
#
#   tools/run_android.sh --push <directory> <device directory>
#
# copies a directory to the device instead, files that changed only.
#
# RUN_ANDROID_LIBS, a colon-separated list of host libraries (a
# sanitizer's runtime), are pushed beside the executable and found
# through LD_LIBRARY_PATH.
set -eu
adb=${ADB:-adb}
if [ "$1" = --push ]; then
    "$adb" shell mkdir -p "$3" > /dev/null
    exec "$adb" push --sync "$2" "$3"
fi
exe=$1
shift
name=$(basename "$exe")
dir=/data/local/tmp/maul-audio/$name.$$
"$adb" shell mkdir -p "$dir" > /dev/null
"$adb" push "$exe" "$dir/$name" > /dev/null 2>&1
libs=""
if [ -n "${RUN_ANDROID_LIBS:-}" ]; then
    for lib in $(printf '%s' "$RUN_ANDROID_LIBS" | tr ':' ' '); do
        "$adb" push "$lib" "$dir/" > /dev/null 2>&1
    done
    libs=" LD_LIBRARY_PATH=$dir"
fi
quoted=""
for arg in "$@"; do
    quoted="$quoted '$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")'"
done
vars=""
for var in $(env | sed -n 's/^\(MAUD_[A-Z0-9_]*\)=.*/\1/p'); do
    eval "value=\$$var"
    vars="$vars $var='$(printf '%s' "$value" | sed "s/'/'\\\\''/g")'"
done
# adb's shell protocol carries the remote exit status.
set +e
"$adb" shell "cd $dir && env$libs$vars ./$name$quoted"
status=$?
"$adb" shell rm -rf "$dir" > /dev/null
exit $status
