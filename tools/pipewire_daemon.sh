#!/bin/bash
# Starts or stops a private PipeWire daemon, its PulseAudio server and
# WirePlumber for the Linux backend tests, with one null sink and one
# null source and no other device, all under one directory, on a session
# bus of their own (WirePlumber exits without one, and a CI container
# has none). CTest runs it as the sound-server fixture; the tests reach
# the daemon with XDG_RUNTIME_DIR=<dir>/run, and the restart tests stop
# and start it with this script (test/test_daemon.h).
#
# usage: pipewire_daemon.sh start|stop <dir>
#        pipewire_daemon.sh signal <dir> <name> <signal>
#
# signal sends a signal (STOP, CONT) to one of the processes it started,
# as the tests that freeze the daemon do.
#
# Each process it starts is recorded in <dir>/<name>.pid with its start
# time, and only a process whose PID and start time both still match is
# ever signalled: a record can outlive its process (a cancelled run, a
# reboot), and by then the PID may be another program's. Every wait is
# bounded and fails with the reason; the daemons' logs are in <dir>.
set -euo pipefail
[ $# -eq 2 ] || [ $# -eq 4 ] || { echo "usage: $0 start|stop <dir> | signal <dir> <name> <signal>" >&2; exit 2; }
action=$1
dir=$2
mkdir -p "$dir"
dir=$(cd "$dir" && pwd)
export XDG_RUNTIME_DIR=$dir/run
export XDG_CONFIG_HOME=$dir/config
export XDG_STATE_HOME=$dir/state
export DBUS_SESSION_BUS_ADDRESS=unix:path=$dir/run/bus
unset PULSE_SERVER PIPEWIRE_REMOTE

# Seconds a step may take before the start fails. A loaded machine took
# under two for each; the bound leaves room for a much slower one.
LIMIT=20

# A process's /proc stat fields from the third on (its state first, its
# start time twentieth): the command name before them may hold spaces.
stat_fields() {
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    echo "${stat##*) }"
}

start_time() {
    local fields
    fields=($(stat_fields "$1")) || return 0
    echo "${fields[19]}"
}

# Whether the record names a live process of ours: the same PID with the
# same start time, not a zombie. In a container without an init (docker
# run without --init), a daemon that exited stays a zombie.
running() {
    local record=$1 pid started fields
    [ -f "$record" ] || return 1
    read -r pid started < "$record" || return 1
    [ -n "$pid" ] || return 1
    fields=($(stat_fields "$pid")) || return 1
    [ "${fields[19]}" = "$started" ] && [ "${fields[0]}" != Z ] && [ "${fields[0]}" != X ]
}

record() { echo "$2 $(start_time "$2")" > "$dir/$1.pid"; }

# Fails the start with the reason and the daemons' logs.
fail() {
    echo "pipewire_daemon: $1; logs in $dir" >&2
    tail -n 20 "$dir"/*.log >&2 2>/dev/null || true
    exit 1
}

# Waits until a command succeeds, LIMIT seconds at most. With a daemon's
# name first, fails at once if that daemon has exited: a daemon that
# refused to start never answers, and waiting longer tells nothing more.
wait_for() {
    local what=$1 daemon=
    shift
    if [ "$1" = --daemon ]; then
        daemon=$2
        shift 2
    fi
    local deadline=$((SECONDS + LIMIT))
    until "$@" >/dev/null 2>&1; do
        if [ -n "$daemon" ] && ! running "$dir/$daemon.pid"; then
            fail "$daemon exited before $what was ready"
        fi
        if [ $SECONDS -ge $deadline ]; then
            fail "$what not ready after $LIMIT s"
        fi
        sleep 0.1
    done
}

# The system's configuration of a daemon without the rt module: in a
# container there is neither realtime scheduling nor a system bus for
# RTKit, and the module only logs that.
without_rt() { # program file
    [ -f "/usr/share/$1/$2" ] || return 0
    awk '/name = libpipewire-module-rt/ { skip = 1; depth = 0 }
         skip { depth += gsub(/\{/, "{") - gsub(/\}/, "}"); if (depth <= 0) skip = 0; next }
         { print }' "/usr/share/$1/$2" > "$dir/config/$1/$2"
}

# Starts a daemon unless the one recorded still runs; launched says
# whether it started one.
launched=false
launch() { # name
    launched=false
    if ! running "$dir/$1.pid"; then
        "$1" > "$dir/$1.log" 2>&1 &
        record "$1" $!
        launched=true
    fi
}

count_nodes() { pw-cli ls Node | grep -c "node.name = \"$1\"" || true; }
has_sink() { pactl list short sinks | grep -q maud-test-sink; }
has_default() { [ "$(pactl get-default-sink)" = maud-test-sink ]; }

case $action in
start)
    mkdir -p "$dir/run" "$dir/state" "$dir/config/wireplumber/main.lua.d" \
        "$dir/config/pipewire/pipewire.conf.d"
    chmod 700 "$dir/run"
    without_rt pipewire pipewire.conf
    without_rt pipewire pipewire-pulse.conf
    without_rt wireplumber wireplumber.conf
    # No JACK server to watch for on a private bus.
    echo 'context.properties = { module.jackdbus-detect = false }' \
        > "$dir/config/pipewire/pipewire.conf.d/90-maud-no-jackdbus.conf"
    # Only the test devices: no sound card, camera or video monitors.
    for m in alsa libcamera v4l2; do
        echo "${m}_monitor.enabled = false" > "$dir/config/wireplumber/main.lua.d/85-maud-no-$m.lua"
    done
    if ! running "$dir/dbus.pid"; then
        rm -f "$dir/run/bus"
        pid=$(dbus-daemon --session --fork --address="$DBUS_SESSION_BUS_ADDRESS" --print-pid=1)
        record dbus "$pid"
    fi
    wait_for "the session bus" test -S "$dir/run/bus"
    launch pipewire
    # A start that finds the daemon running reuses it as it is (CTest
    # runs the fixture's setup again when it repeats tests): the test
    # nodes live as long as the daemon, and making them again would give
    # two devices of each name.
    fresh=$launched
    wait_for "pipewire" --daemon pipewire pw-cli info 0
    # The PulseAudio directory exists before pipewire-pulse starts: it
    # checks for the directory, then makes it, and fails if another
    # process made it between the two. pactl, polling below, makes it
    # whenever it looks for a server.
    mkdir -p -m 700 "$dir/run/pulse"
    launch pipewire-pulse
    wait_for "pipewire-pulse" --daemon pipewire-pulse pactl info
    launch wireplumber
    if $fresh; then
        pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=maud-test-sink node.description="Maud test sink" media.class=Audio/Sink audio.rate=48000 audio.channels=2 audio.position=[FL FR] object.linger=true }' >/dev/null
        pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=maud-test-source node.description="Maud test source ğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğğ音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音音" media.class=Audio/Source/Virtual audio.rate=48000 audio.channels=2 audio.position=[FL FR] object.linger=true }' >/dev/null
    fi
    wait_for "the test sink" has_sink
    for node in maud-test-sink maud-test-source; do
        count=$(count_nodes "$node")
        [ "$count" = 1 ] || fail "$count nodes named $node, where there must be one"
    done
    # WirePlumber makes the only sink the default once it has seen it.
    wait_for "the test sink as the default" has_default
    ;;
stop)
    for name in wireplumber pipewire-pulse pipewire dbus; do
        record_file=$dir/$name.pid
        if running "$record_file"; then
            read -r pid _ < "$record_file"
            kill "$pid"
            deadline=$((SECONDS + LIMIT))
            while running "$record_file"; do
                if [ $SECONDS -ge $deadline ]; then
                    echo "pipewire_daemon: $name (PID $pid) still running after $LIMIT s" >&2
                    exit 1
                fi
                sleep 0.1
            done
        fi
        rm -f "$record_file"
    done
    ;;
signal)
    record_file=$dir/$3.pid
    running "$record_file" || { echo "pipewire_daemon: $3 is not running" >&2; exit 1; }
    read -r pid _ < "$record_file"
    kill "-$4" "$pid"
    ;;
*)
    echo "usage: $0 start|stop <dir> | signal <dir> <name> <signal>" >&2
    exit 2
    ;;
esac
