#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Gives a macOS CI runner, which has no audio hardware, a virtual audio
# devices for the Core Audio and iOS suites: BlackHole's 2-channel device,
# and its 16-channel one for the channel order, from Homebrew, then the
# audio server restarted so that it lists them. In a GitHub Actions job
# it also lets the Core Audio suite restart the audio server itself
# (MAUD_TEST_COREAUDIO_RESTART), which a developer's own machine must not
# do. For CI only.
set -euo pipefail
brew install --cask blackhole-2ch blackhole-16ch
sudo killall coreaudiod || true
# The server starts again on the next client; wait for it to list both,
# a minute at most.
listed() {
    local devices
    devices=$(system_profiler SPAudioDataType 2>/dev/null)
    grep -q "BlackHole 2ch" <<<"$devices" && grep -q "BlackHole 16ch" <<<"$devices"
}
for _ in $(seq 60); do
    listed && break
    sleep 1
done
listed || { echo "BlackHole 2ch and 16ch are not both listed" >&2; exit 1; }
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "MAUD_TEST_COREAUDIO_RESTART=sudo killall coreaudiod" >> "$GITHUB_ENV"
fi
