#!/bin/bash
# Starts or stops a private PipeWire daemon, its PulseAudio server and
# WirePlumber for the PipeWire and PulseAudio tests, with one null sink
# and one null source and no other device, all under one directory, on
# a session bus of their own (WirePlumber exits without one, and a CI
# runner has none).
# The tests reach it with XDG_RUNTIME_DIR=<dir>/run, and the restart
# tests stop and start it with this script (MAUD_TEST_PIPEWIRE_STOP and
# MAUD_TEST_PIPEWIRE_START).
#
# usage: pipewire_daemon.sh start|stop <dir>
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 start|stop <dir>" >&2; exit 2; }
action=$1
dir=$2
export XDG_RUNTIME_DIR=$dir/run
export XDG_CONFIG_HOME=$dir/config
export XDG_STATE_HOME=$dir/state
export DBUS_SESSION_BUS_ADDRESS=unix:path=$dir/run/bus

running() { [ -f "$1" ] && kill -0 "$(cat "$1")" 2>/dev/null; }

start() { # name, seconds to settle
    if ! running "$dir/$1.pid"; then
        "$1" > "$dir/$1.log" 2>&1 &
        echo $! > "$dir/$1.pid"
        sleep "$2"
    fi
}

case $action in
start)
    mkdir -p "$dir/run" "$dir/state" "$dir/config/wireplumber/main.lua.d"
    chmod 700 "$dir/run"
    # Only the test devices: no sound card, camera or video monitors.
    for m in alsa libcamera v4l2; do
        echo "${m}_monitor.enabled = false" > "$dir/config/wireplumber/main.lua.d/85-maud-no-$m.lua"
    done
    if ! running "$dir/dbus.pid"; then
        rm -f "$dir/run/bus"
        dbus-daemon --session --fork --address="$DBUS_SESSION_BUS_ADDRESS" --print-pid=1 > "$dir/dbus.pid"
    fi
    start pipewire 1
    start pipewire-pulse 0
    start wireplumber 2
    if ! pw-cli ls Node | grep -q maud-test-sink; then
        pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=maud-test-sink node.description="Maud test sink" media.class=Audio/Sink audio.rate=48000 audio.channels=2 audio.position=[FL FR] object.linger=true }' >/dev/null
        pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=maud-test-source node.description="Maud test source" media.class=Audio/Source/Virtual audio.rate=48000 audio.channels=2 audio.position=[FL FR] object.linger=true }' >/dev/null
        sleep 1
    fi
    ;;
stop)
    for name in wireplumber pipewire-pulse pipewire dbus; do
        pid_file=$dir/$name.pid
        if running "$pid_file"; then
            kill "$(cat "$pid_file")"
            for _ in $(seq 50); do kill -0 "$(cat "$pid_file")" 2>/dev/null || break; sleep 0.1; done
        fi
        rm -f "$pid_file"
    done
    ;;
*)
    echo "usage: $0 start|stop <dir>" >&2
    exit 2
    ;;
esac
