#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Fills a corpus directory per fuzz target with its committed seeds:
# test/corpus/<target>/, and the shipped files behind the byte that makes
# the target read them as such (CMakeLists.txt and the targets say what
# the first byte chooses): the HRTF set behind 0x01, the bakes behind
# 0x03 (fields of order 1, as the office bakes have). fuzz_scene's seeds
# are built by tools/fuzz_scene_seeds.py. Files already in the
# directories (a corpus carried from an earlier run) stay.
#
#   tools/fuzz_seeds.sh <corpus root>
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: $0 <corpus root>" >&2; exit 2; }
cd "$(dirname "$0")/.."
root=$1
for target in fuzz_hrtf fuzz_bake; do
    mkdir -p "$root/$target"
    cp test/corpus/$target/* "$root/$target/"
done
{ printf '\001'; cat data/hrtf/sadie2-ku100-48k.maudhrtf; } > "$root/fuzz_hrtf/seed-shipped"
for bake in data/bake/*.maudbake; do
    { printf '\003'; cat "$bake"; } > "$root/fuzz_bake/seed-$(basename "$bake" .maudbake)"
done
python3 tools/fuzz_scene_seeds.py "$root/fuzz_scene"
