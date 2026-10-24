#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The symbols a shared build exports are the public functions, no more
# and no fewer: every function the public headers declare, and nothing
# else, so internal functions, tables and globals stay out of the binary
# interface. The headers are the ones tools/gen_api.py reads: the
# library's include/<MAUL_LIBRARY>/ and each part's that
# tools/source-dirs.txt lists. Their functions are what gen_api.collect
# finds, less the static inline helpers, which no library exports; the
# library's name and prefix come from CMakeLists.txt through it. Every
# defined dynamic symbol counts, whatever its name or kind, and a
# versioned one by its name before the '@': one without the prefix, one
# holding data, or one no header declares fails it, as does a declared
# function missing from the library. Each failure names the symbol.
#
#   python3 tools/check_exports.py [--nm NM] out/linux-shared/lib<library>.so
#
# Reads the dynamic symbol table with nm (CMake passes CMAKE_NM); for ELF
# shared libraries.

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_api  # noqa: E402

# nm's letters for code: global, local or weak.
CODE = set("TtWw")


def declared():
    """The names of the functions the public headers declare."""
    names = set()
    sections = [(gen_api.HEADER_DIR, gen_api.LIB)]
    sections += [(folder, name) for name, folder in gen_api.parts()]
    for folder, umbrella in sections:
        for header in gen_api.header_order(folder, umbrella):
            with open(os.path.join(folder, header), encoding="utf-8") as f:
                lines = f.read().split("\n")
            for _, signature in gen_api.collect(lines):
                found = re.search(r"\b(" + gen_api.PREFIX + r"\w+)\s*\(", signature)
                if found and "static" not in signature[:found.start()].split():
                    names.add(found.group(1))
    return names


def exported(nm, library):
    """Each defined dynamic symbol's name, before any version, and nm's
    letter for it."""
    out = subprocess.run([nm, "-D", "--defined-only", library], check=True,
                         capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 3:
            name, _, version = fields[2].partition("@")
            found.append((name, version.lstrip("@"), fields[1]))
    # A version script makes the linker define each version node as an
    # absolute symbol named after it (GNU nm prints V, llvm-nm V@@V); it
    # is no function or data of the library's.
    versions = {version for _, version, _ in found if version}
    return {name: letter for name, _, letter in found
            if not (letter == "A" and name in versions)}


def main():
    parser = argparse.ArgumentParser(description="Checks a shared library's exports.")
    parser.add_argument("--nm", default="nm", help="the nm to read the library with")
    parser.add_argument("library")
    args = parser.parse_args()
    public = declared()
    symbols = exported(args.nm, args.library)
    prefix = gen_api.PREFIX
    wrong = []
    for name, letter in sorted(symbols.items()):
        if not name.startswith(prefix):
            wrong.append(f"exported without the {prefix} prefix: {name}")
        elif letter not in CODE:
            wrong.append(f"exported data: {name}")
        elif name not in public:
            wrong.append(f"exported but not public: {name}")
    for name in sorted(public - symbols.keys()):
        wrong.append(f"public but not exported: {name}")
    for line in wrong:
        print(line)
    if wrong or not public:
        return 1
    print(f"exports: the {len(public)} public functions, nothing else")
    return 0


if __name__ == "__main__":
    sys.exit(main())
