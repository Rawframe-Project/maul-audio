#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
"""Checks that no engine concept is named in Maul Audio's public API.

Any C or C++ program must be able to use Maul Audio: a game, a DAW-style
tool, a voice app (record maud-0001). This reads the public headers'
identifiers, comments and strings left out, splits each into words at
case changes, underscores and digits, and refuses the words that name an
engine's concepts, the engine's audio side among them: its mixer, buses
and emitters. Whole words, so that Busy and Gamut pass;
"source", "listener" and "voice" stay allowed, the library's own sound
sources, listeners and voice processing.

Usage: check_litmus.py [ROOT], ROOT the repository (its own by default).
"""

import pathlib
import re
import sys

REFUSED = {"rawframe", "kest", "world", "worlds", "entity", "entities", "component", "components",
           "schema", "schemas", "asset", "assets", "game", "games", "scripting", "scriptable",
           "emitter", "emitters", "mixer", "mixers", "bus", "buses"}
HEADER_DIRS = ["include"]
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\'')
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+")


def blank(match):
    """Keeps a match's newlines, so lines still count."""
    return "\n" * match.group(0).count("\n")


def words(identifier):
    """An identifier's words, lower case."""
    return [word.lower() for word in WORD.findall(identifier)]


def check(root):
    findings = []
    for directory in HEADER_DIRS:
        for header in sorted((root / directory).rglob("*.h")):
            text = COMMENT.sub(blank, header.read_text(encoding="utf-8"))
            text = STRING.sub(blank, text)
            for number, line in enumerate(text.splitlines(), 1):
                for identifier in IDENTIFIER.findall(line):
                    refused = REFUSED.intersection(words(identifier))
                    if refused:
                        findings.append(f"{header.relative_to(root)}:{number}: {identifier} "
                                        f"names {', '.join(sorted(refused))}")
    return findings


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).parent.parent
    findings = check(root.resolve())
    for finding in findings:
        print(finding)
    print(f"litmus: {len(findings)} engine concepts named in the public API")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
