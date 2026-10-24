#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Gives a macOS CI runner, which has no audio hardware, a virtual audio
# device for the Core Audio and iOS suites: BlackHole, from Homebrew, then
# the audio server restarted so that it lists it. In a GitHub Actions job
# it also lets the Core Audio suite restart the audio server itself
# (MAUD_TEST_COREAUDIO_RESTART), which a developer's own machine must not
# do. For CI only.
set -euo pipefail
brew install --cask blackhole-2ch
sudo killall coreaudiod || true
# The server starts again on the next client; wait for it to list
# BlackHole, a minute at most.
for _ in $(seq 60); do
    system_profiler SPAudioDataType 2>/dev/null | grep -q BlackHole && break
    sleep 1
done
system_profiler SPAudioDataType | grep -q BlackHole || { echo "BlackHole is not listed" >&2; exit 1; }
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "MAUD_TEST_COREAUDIO_RESTART=sudo killall coreaudiod" >> "$GITHUB_ENV"
fi
