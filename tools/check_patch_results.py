#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The rule for patch releases from 1.0.0: a patch release never changes
# results. Programs ship bakes and tune their sound by ear, so a patch
# release, which a program takes without a second look, renders and bakes
# exactly as the release before it. A change of results ships in a minor
# release, whose changelog names it and who hears it.
#
# When the version in include/maul-audio/base.h is a patch release of the
# last release tag (same major and minor, from 1.0.0 on, higher patch),
# this compares the golden renders (data/golden/) and the golden bake
# (data/bake/) with the tag's and refuses any file that changed, appeared
# or left. Any other version passes. --self-test checks the decision
# against a table of versions.
#
# usage: check_patch_results.py [--self-test]

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = ("data/golden", "data/bake")

# (last tag, version, whether the golden data must match the tag's)
DECISIONS = (
    ((0, 2, 0), (0, 2, 0), False),
    ((0, 2, 0), (0, 2, 1), False),
    ((0, 2, 0), (0, 3, 0), False),
    ((0, 9, 3), (1, 0, 0), False),
    ((1, 0, 0), (1, 0, 0), False),
    ((1, 0, 0), (1, 0, 1), True),
    ((1, 2, 4), (1, 2, 5), True),
    ((1, 2, 4), (1, 3, 0), False),
)


def is_patch_release(tag, version):
    """Whether version is a patch release of tag from 1.0.0 on."""
    return version[0] >= 1 and version[:2] == tag[:2] and version[2] > tag[2]


def head_version():
    path = os.path.join(ROOT, "include", "maul-audio", "base.h")
    text = open(path, encoding="utf-8").read()
    parts = []
    for name in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(r"#define MAUD_VERSION_" + name + r" (\d+)", text)
        if match is None:
            sys.exit(f"check_patch_results.py: no MAUD_VERSION_{name} in include/maul-audio/base.h")
        parts.append(int(match.group(1)))
    return tuple(parts)


def git(*args):
    return subprocess.run(
        ("git", "-C", ROOT) + args, check=True, capture_output=True, text=True
    ).stdout


def last_tag():
    tags = [t for t in git("tag", "--list", "v*").split() if re.fullmatch(r"v\d+\.\d+\.\d+", t)]
    if not tags:
        sys.exit("check_patch_results.py: no release tag; fetch the tags first")
    return max(tags, key=lambda t: tuple(int(p) for p in t[1:].split(".")))


def self_test():
    wrong = [
        f"{tag} to {version}: expected {expected}"
        for tag, version, expected in DECISIONS
        if is_patch_release(tag, version) != expected
    ]
    for line in wrong:
        print(f"check_patch_results.py self-test: {line}")
    if wrong:
        sys.exit(1)


def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return
    if sys.argv[1:]:
        sys.exit("usage: check_patch_results.py [--self-test]")
    tag = last_tag()
    tag_version = tuple(int(p) for p in tag[1:].split("."))
    if not is_patch_release(tag_version, head_version()):
        return
    changed = git("diff", "--name-only", "--no-renames", tag, "--", *GOLDEN).split()
    for path in changed:
        print(
            f"{path} differs from {tag}; a change of results ships in a minor release "
            "(docs/operations/release.md)"
        )
    if changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
