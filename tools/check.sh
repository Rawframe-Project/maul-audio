#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in three tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            source rules, the test hygiene check,
#                            formatting, the documentation checks,
#                            clang-tidy in Release and in Debug on the
#                            changed sources, and in the
#                            linux-clang-20-asan preset
#                            (assertions on) the suites the changes reach.
#                            An integration suite runs only when a change
#                            reaches it by name (its own file, the daemon
#                            header or script), against the sound-server
#                            fixture. Under two minutes; run it before
#                            every push.
#   tools/check.sh pr        what the pull request check runs on Linux:
#                            every check, clang-tidy, the linux-gcc-14 and
#                            linux-clang-20-asan presets with the sound
#                            server suites, and a short fuzzing pass.
#   tools/check.sh full      also the other Linux presets (ThreadSanitizer,
#                            shared, each part alone, the private backend,
#                            Clang Release) and longer fuzzing.
#
# The builds are the CI cells' CMake presets (CMakePresets.json), in
# out/<preset>; the other trees go to out/check-*. Needs gcc-14, clang-20,
# clang-tidy-20, ninja, the clang-format version CI pins, and for the
# sound server suites pipewire, pipewire-pulse, wireplumber, pactl and
# dbus-daemon: the organization's CI image has them all. The org
# documentation check runs when the organization's .github checkout is at
# ORG_DEFAULTS (default: ../../.github from the repository).
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
out=$root/out
mkdir -p "$out"
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}

step() { printf '\n== %s\n' "$*"; }

# A tree outside the presets (clang-tidy, fuzzing).
configure() { # name, then cmake arguments
    local name=check-$1
    shift
    cmake -S "$root" -B "$out/$name" -G Ninja -DMAUL_AUDIO_WERROR=ON "$@" >"$out/$name.log"
}

# A CI cell: its configure, build and test presets.
preset() {
    local name=$1
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    python3 tools/check_test_hygiene.py --ctest "$out/$name"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    ctest --preset "$name" -j "$jobs"
}

# The changed-files tier's suites in a CI cell's build: the suites the
# first label filter picks (every suite when it is empty), and the
# integration suites the second picks (none when it is empty) against the
# sound-server fixture, which must be able to start here. Filters that
# pick nothing fail, as the presets' noTestsAction does. A test preset's
# own label exclusion overrides an -LE given beside it, so this runs the
# build directory with the preset's timeout instead.
count_tests() { # build directory, then ctest selection arguments
    local dir=$1
    shift
    ctest --test-dir "$dir" -N "$@" | sed -n 's/^Total Tests: //p'
}

# Stops the sound-server fixture's daemons in one directory, thawing any
# a suite froze first. The script signals only the processes it started
# there, so a fixture another run started elsewhere is never touched.
stop_fixture() {
    local name
    for name in wireplumber pipewire-pulse pipewire dbus; do
        tools/pipewire_daemon.sh signal "$1" "$name" CONT 2>/dev/null || true
    done
    tools/pipewire_daemon.sh stop "$1" || true
}

preset_changed() {
    local name=$1 dir=out/$1 units integration=0
    local pick=(-LE '^(integration|quarantine)$') reach=(-L "$3" -L '^integration$' -LE '^quarantine$')
    [ -z "$2" ] || pick=(-L "$2" "${pick[@]}")
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    python3 tools/check_test_hygiene.py --ctest "$dir"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    units=$(count_tests "$dir" "${pick[@]}")
    [ -z "$3" ] || integration=$(count_tests "$dir" "${reach[@]}")
    if [ "$units" -eq 0 ] && [ "$integration" -eq 0 ]; then
        echo "no suite in $dir matches ${2:-every label} or ${3:-no label}" >&2
        exit 1
    fi
    if [ "$integration" -gt 0 ] && [ "$(count_tests "$dir" -R '^sound_server_start$')" -eq 0 ]; then
        echo "the changes reach integration suites, but the sound-server fixture cannot start" \
            "here: it needs pipewire, pipewire-pulse, wireplumber, pw-cli, pactl and dbus-daemon" \
            "(the CI image has them)" >&2
        exit 1
    fi
    local run=(ctest --test-dir "$dir" -j "$jobs" --timeout 300 --output-on-failure --no-tests=error)
    if [ "$units" -gt 0 ]; then
        "${run[@]}" "${pick[@]}"
    fi
    if [ "$integration" -gt 0 ]; then
        step "integration suites against the sound-server fixture"
        # CTest stops the fixture after the run, but not when the run is
        # interrupted, killed or timed out: then this does, for this build
        # tree's fixture.
        # shellcheck disable=SC2064 # the directory is fixed now
        trap "stop_fixture '$root/$dir/sound-server'" EXIT
        trap 'exit 129' HUP
        trap 'exit 130' INT
        trap 'exit 143' TERM
        "${run[@]}" "${reach[@]}"
    fi
}

# The suites of each area, for the changed-files tier.
base="base allocator buffer layout"
device="context stream stream_open loopback outputs objects device duplex focus follow device_sync
    form lifecycle call_back_in sample_convert xrun retry stamp worker utf8 device_refusals refusals
    versions guide"
voice="voice_detector gain_control noise_suppressor echo_canceller voice_silence voice_refusals
    voice_dsp"
effects="ambisonics binaural binaural_decoder hrtf near_field direct_effect band_eq reverb octave_eq
    speaker_decoder speaker_panner effect_refusals render_refusals spatial_silence golden guide"
scene="scene scene_instances scene_refusals bvh watertight occlusion transmission"
simulation="spatializer spatializer_refusals spatializer_threads spatializer_reverb
    spatializer_reflections spatializer_probes spatializer_pathing spatializer_bake
    spatializer_call_back_in path_refine path_search bake_file portable_math probe_graph real_fft
    reflection_response reverb_estimate partitioned staged golden"

# The ctest labels a changed path reaches; "all" when it reaches every
# suite (the base, the build, the test harness).
labels_for() {
    local name
    name=$(basename "$1")
    name=${name%.*}
    case "$1" in
    *.md | LICENSE | .github/* | tools/check_*.py | tools/build_docs.py | tools/gen_api.py | \
        tools/cost_table.py | java/* | samples/* | bench/* | test/corpus/* | \
        test/fuzz_* | test/web/* | test/ios/* | test/android/* | data/hrtf/apache-2.0/*) ;;
    docs/user/*) echo guide ;;
    test/test_daemon.h) echo "alsa pipewire pipewire_restart pulse" ;;
    tools/pipewire_daemon.sh) echo "alsa pipewire pipewire_restart pulse stall guide_device" ;;
    tools/*.sh) ;;
    test/test_harness.h | test/test_signals.h | test/test_names.h | test/test_clock.h | \
        CMakeLists.txt | CMakePresets.json | cmake/* | src/base.c | src/allocator.* | \
        include/maul-audio/base.h) echo all ;;
    # Suites built only for another platform, or only with the private
    # backend: no Linux build here has them, and their own builds run them.
    test/test_aaudio.c | test/test_android_java.c | test/test_coreaudio.c | \
        test/test_coreaudio_form.c | test/test_coreaudio_listen.c | test/test_wasapi.c | \
        test/test_wasapi_voice.c | test/test_web_capture.c | test/test_web_transport.c | \
        test/test_private_backend.c) ;;
    test/test_*.c) echo "${name#test_}" ;;
    src/buffer.* | src/layout.* | src/fft.* | src/real_fft.* | include/maul-audio/buffer.h | \
        include/maul-audio/layout.h) echo "$base $voice $effects $simulation" ;;
    src/vad_core.* | src/voice* | src/gain_control.* | src/noise_suppressor.* | src/echo_* | \
        include/maul-audio/voice.h) echo "$voice" ;;
    src/pipewire_* | src/backend_pipewire.c) echo "pipewire_parse pipewire_route loaders" ;;
    src/alsa_* | src/backend_alsa.c) echo "alsa_chmap alsa_recover alsa_hotplug loaders" ;;
    src/pulse_* | src/backend_pulse.c) echo loaders ;;
    src/wasapi_format.*) echo wasapi_format ;;
    src/wasapi_* | src/backend_wasapi.c | src/coreaudio_* | src/backend_coreaudio.c | src/ios_* | \
        src/backend_ios.c | src/apple_* | src/aaudio_* | src/backend_aaudio.c | src/web_* | \
        src/backend_web.c) ;;
    src/context.* | src/stream* | src/device.* | src/follow.* | src/notify.* | src/period.* | \
        src/focus.* | src/lifecycle.* | src/offline.* | src/backend_offline.c | src/backend.h | \
        src/clock.* | src/duplex.* | src/form.* | src/sample_convert.* | src/thread.* | \
        src/worker.* | src/xrun.* | src/retry.* | src/context_core.h | include/maul-audio/context.h | \
        include/maul-audio/device.h | include/maul-audio/stream.h | include/maul-audio/notification.h | \
        include/maul-audio/offline.h | include/maul-audio/objects.h | include/maul-audio/focus.h)
        echo "$device" ;;
    src/scene* | src/bvh.* | src/watertight.* | include/maul-audio/scene.h) echo "$scene $simulation" ;;
    src/spatializer* | src/occlusion.* | src/transmission.* | src/pathing.* | src/path_* | \
        src/probe_* | src/bake_file.* | src/reverb_estimate.* | src/reflection* | src/partitioned.* | \
        src/staged.* | src/portable_math.* | src/direct_step.* | src/crc32.* | \
        include/maul-audio/spatializer.h) echo "$simulation" ;;
    src/* | include/*) echo "$effects" ;;
    *) echo all ;;
    esac
}

source_checks() {
    step "source, module, length, documentation, snippet, API and test hygiene rules"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_float_env.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    python3 tools/check_litmus.py
    python3 tools/check_patch_results.py --self-test
    python3 tools/check_patch_results.py
    python3 tools/cost_table.py --check
    python3 tools/check_test_hygiene.py --self-test
    python3 tools/check_test_hygiene.py
    if [ -f "$org/scripts/docs_check.py" ]; then
        python3 "$org/scripts/docs_check.py" --strict --exclude data/hrtf/apache-2.0
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
    if [ -f "$org/maul/family_sync.py" ]; then
        python3 "$org/maul/family_sync.py" --check --root "$root/.." maul-audio
    fi
}

all_c_files() { git ls-files 'include/*.h' 'src/*.[chm]' 'test/*.[chm]' 'tools/*.[ch]' 'bench/*.[ch]' 'samples/*.[ch]'; }

# clang-tidy as CI's checks job runs it: on the Release preset, where the
# assertions compile out, and with them live. Each hides findings the
# other shows. It checks the Linux build's sources under src/: every one
# for "all", otherwise those among the paths given, and none when no path
# given is one of them.
tidy() { # "all", or the paths to check
    local build tree files
    for build in Release Debug; do
        tree=$out/check-tidy-$build
        cmake --preset linux-clang-20 -B "$tree" -DCMAKE_BUILD_TYPE=$build \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAUL_AUDIO_BUILD_TESTS=OFF >"$tree.log"
        mapfile -t files < <(python3 -c '
import json, os, sys
tree, root, *paths = sys.argv[1:]
files = {e["file"] for e in json.load(open(tree + "/compile_commands.json")) if "/src/" in e["file"]}
if paths != ["all"]:
    given = {os.path.realpath(os.path.join(root, p)) for p in paths}
    files = {f for f in files if os.path.realpath(f) in given}
sys.stdout.write("".join(f + "\n" for f in sorted(files)))' "$tree" "$root" "$@")
        if [ "$1" = all ] && [ ${#files[@]} -eq 0 ]; then
            echo "no sources for clang-tidy" >&2
            exit 1
        fi
        step "clang-tidy, $build, on ${#files[@]} of the Linux build's sources"
        [ ${#files[@]} -gt 0 ] || continue
        printf '%s\n' "${files[@]}" | xargs -P "$jobs" -n 1 clang-tidy-20 -p "$tree" --quiet \
            --warnings-as-errors='*' 2>/dev/null
    done
}

if [ "$tier" = changed ]; then
    merge_base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
    mapfile -t changed < <({
        git diff --name-only "$merge_base"
        git ls-files --others --exclude-standard
    } | sort -u)
    if [ ${#changed[@]} -eq 0 ]; then
        echo "nothing changed against origin/main"
        exit 0
    fi
    source_checks
    mapfile -t c_files < <(printf '%s\n' "${changed[@]}" | grep -E '\.[chm]$' || true)
    present=()
    for f in "${c_files[@]+"${c_files[@]}"}"; do [ -f "$f" ] && present+=("$f"); done
    if [ ${#present[@]} -gt 0 ]; then
        step "clang-format on ${#present[@]} changed files"
        clang-format --dry-run -Werror "${present[@]}"
    fi
    sources=()
    for f in "${present[@]+"${present[@]}"}"; do [[ $f == src/* ]] && sources+=("$f"); done
    if [ ${#sources[@]} -gt 0 ]; then
        tidy "${sources[@]}"
    fi
    labels=""
    for f in "${changed[@]}"; do labels="$labels $(labels_for "$f")"; done
    if [ -z "${labels// /}" ]; then
        echo "no suite reached by the changes"
        exit 0
    fi
    # A change that reaches every suite runs every unit suite, and still
    # the integration suites the other changes reach by name.
    regex=$(tr ' ' '\n' <<<"$labels" | grep -vx -e all -e '' | sort -u | paste -sd '|' || true)
    named=${regex:+^($regex)\$}
    if [[ " $labels " == *" all "* ]]; then
        echo "every suite${regex:+, and the integration suites among $regex}"
        preset_changed linux-clang-20-asan "" "$named"
    else
        echo "suites labeled $regex"
        preset_changed linux-clang-20-asan "$named" "$named"
    fi
    exit 0
fi

source_checks

step "clang-format on every file"
mapfile -t files < <(all_c_files)
[ ${#files[@]} -gt 0 ] || { echo "no C files found" >&2; exit 1; }
clang-format --dry-run -Werror "${files[@]}"

tidy all

preset linux-gcc-14
preset linux-clang-20-asan

fuzz_seconds=10
[ "$tier" = full ] && fuzz_seconds=60
step "fuzzing, $fuzz_seconds seconds per target"
configure fuzz -DCMAKE_C_COMPILER=clang-20 -DMAUL_AUDIO_FUZZ=ON -DMAUL_AUDIO_DEVICE=OFF \
    -DMAUL_AUDIO_BUILD_TESTS=OFF -DMAUL_AUDIO_BUILD_BENCH=OFF -DMAUL_AUDIO_BUILD_SAMPLES=OFF
cmake --build "$out/check-fuzz" -j "$jobs" >>"$out/check-fuzz.log"
tools/fuzz_seeds.sh "$out/corpus"
for target in "$out"/check-fuzz/fuzz_*; do
    name=$(basename "$target")
    "$target" -max_total_time=$fuzz_seconds "$out/corpus/$name" >"$out/$name.log" 2>&1 ||
        {
            tail -30 "$out/$name.log"
            exit 1
        }
    echo "$name: no finding"
done

[ "$tier" = full ] || exit 0

for name in linux-clang-20-tsan linux-shared linux-device-only linux-spatial-only linux-private-backend \
    linux-clang-20; do
    preset "$name"
done

step "all checks passed"
