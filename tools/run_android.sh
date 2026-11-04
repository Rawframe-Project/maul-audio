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
#
# adb can lose its connection to the device ("error: closed") while
# tests start side by side. The steps that only make directories, move
# files or remove them are idempotent, so each is tried up to three
# times. Each run of the test writes its exit status to a file of its
# own on the device (status.1, status.2, status.3), and its status is
# that file's: a run that completed keeps it, whatever it was, and a run
# adb lost and adbd left running cannot write another's. Only when the
# device answers that the run's file is missing, the run not having
# completed, is it run again, up to three runs in all, saying so; when
# even that answer is lost three times, the test fails.
set -eu
adb=${ADB:-adb}

# An idempotent adb step, tried up to three times while adb fails.
again() {
    tries=1
    until "$adb" "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

if [ "$1" = --push ]; then
    again shell mkdir -p "$3" > /dev/null
    again push --sync "$2" "$3"
    exit
fi
exe=$1
shift
name=$(basename "$exe")
dir=/data/local/tmp/maul-audio/$name.$$
# Fresh, so that a status file an earlier run of the same name and
# process left (its removal lost) cannot be read as this run's.
again shell "rm -rf $dir && mkdir -p $dir" > /dev/null
again push "$exe" "$dir/$name" > /dev/null 2>&1
libs=""
if [ -n "${RUN_ANDROID_LIBS:-}" ]; then
    for lib in $(printf '%s' "$RUN_ANDROID_LIBS" | tr ':' ' '); do
        again push "$lib" "$dir/" > /dev/null 2>&1
    done
    libs=" LD_LIBRARY_PATH=$dir"
fi
quoted=""
for arg in "$@"; do
    quoted="$quoted '$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")'"
done
vars=""
for var in $(env | sed -n 's/^\(MAUD_[A-Z0-9_]*\)=.*/\1/p'); do
    value=""
    eval "value=\$$var"
    vars="$vars $var='$(printf '%s' "$value" | sed "s/'/'\\\\''/g")'"
done
# The status is the one the device recorded: adb's own is its
# transport's when the connection drops.
run=1
while :; do
    "$adb" shell "cd $dir && { env$libs$vars ./$name$quoted; echo \$? > status.$run; }" || true
    if ! answer=$(again shell "cat $dir/status.$run 2> /dev/null || echo missing"); then
        echo "run_android.sh: $name's status could not be read: adb lost the device 3 times" >&2
        status=1
        break
    fi
    answer=$(printf '%s' "$answer" | tr -d '\r')
    case $answer in
    missing) ;;
    '' | *[!0-9]*)
        echo "run_android.sh: $name's status file held '$answer'" >&2
        status=1
        break
        ;;
    *)
        status=$answer
        break
        ;;
    esac
    if [ "$run" -eq 3 ]; then
        echo "run_android.sh: $name never completed: adb lost the device on all 3 runs" >&2
        status=1
        break
    fi
    run=$((run + 1))
    echo "run_android.sh: adb lost the device before $name completed; run $run of 3" >&2
done
# The verdict stands whether or not the directory goes.
again shell rm -rf "$dir" > /dev/null || true
exit "$status"
