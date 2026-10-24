#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in three tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            source rules, formatting, the documentation
#                            checks, and in the linux-clang-20-asan preset
#                            (assertions on) the suites the changes reach,
#                            the integration suites left out. Under two
#                            minutes; run it before every push.
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
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    ctest --preset "$name" -j "$jobs"
}

# A CI cell's build with only the suites a label filter picks. A test
# preset's own label exclusion overrides an -LE given beside it, so this
# runs the build directory with the preset's timeout instead.
preset_labeled() {
    local name=$1
    shift
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    ctest --test-dir "out/$name" -j "$jobs" --timeout 300 --output-on-failure --no-tests=error "$@"
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
        tools/cost_table.py | tools/*.sh | java/* | samples/* | bench/* | test/corpus/* | \
        test/fuzz_* | test/web/* | test/ios/* | test/android/* | data/hrtf/apache-2.0/*) ;;
    docs/user/*) echo guide ;;
    test/test_harness.h | test/test_signals.h | test/test_names.h | test/test_clock.h | \
        CMakeLists.txt | CMakePresets.json | cmake/* | src/base.c | src/allocator.* | \
        include/maul-audio/base.h) echo all ;;
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
    step "source, module, length, documentation, snippet and API rules"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_float_env.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    python3 tools/check_litmus.py
    python3 tools/cost_table.py --check
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
    labels=""
    for f in "${changed[@]}"; do labels="$labels $(labels_for "$f")"; done
    if [ -z "${labels// /}" ]; then
        echo "no suite reached by the changes"
        exit 0
    fi
    if [[ " $labels " == *" all "* ]]; then
        preset_labeled linux-clang-20-asan -LE '^(integration|quarantine)$'
    else
        regex=$(printf '%s\n' $labels | sort -u | paste -sd '|')
        echo "suites labeled $regex"
        preset_labeled linux-clang-20-asan -L "^($regex)\$" -LE '^(integration|quarantine)$'
    fi
    exit 0
fi

source_checks

step "clang-format on every file"
mapfile -t files < <(all_c_files)
[ ${#files[@]} -gt 0 ] || { echo "no C files found" >&2; exit 1; }
clang-format --dry-run -Werror "${files[@]}"

step "clang-tidy over the Linux build's sources"
configure tidy -DCMAKE_C_COMPILER=clang-20 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAUL_AUDIO_BUILD_TESTS=OFF
cmake --build "$out/check-tidy" -j "$jobs" >>"$out/check-tidy.log"
mapfile -t tidy < <(python3 -c "import json; print('\n'.join(sorted({e['file'] for e in json.load(open('$out/check-tidy/compile_commands.json')) if '/src/' in e['file']})))")
[ ${#tidy[@]} -gt 0 ] || { echo "no sources for clang-tidy" >&2; exit 1; }
for f in "${tidy[@]}"; do clang-tidy-20 -p "$out/check-tidy" --quiet --warnings-as-errors='*' "$f" 2>/dev/null; done

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
