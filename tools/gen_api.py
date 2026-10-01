#!/usr/bin/env python3
# Generates docs/api.md from the public headers: every function
# declaration with the documentation comment above it, grouped by
# header. Plain text extraction with no compiler, so the reference
# shows exactly what the headers say. The same script serves every Maul
# library; it reads the library's name and prefix from CMakeLists.txt.
#
# Usage: python3 tools/gen_api.py  (from the repository root)

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cmake_setting(name):
    """A value the library's CMakeLists.txt sets with set(NAME value)."""
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        match = re.search(r"^set\(" + name + r" \"?([^\")]+)\"?\)", f.read(), re.M)
    if not match:
        raise SystemExit("CMakeLists.txt does not set " + name)
    return match.group(1)


LIB = cmake_setting("MAUL_LIBRARY")
PREFIX = cmake_setting("MAUL_API_PREFIX")
TITLE = cmake_setting("MAUL_TITLE")
HEADER_DIR = os.path.join(ROOT, "include", LIB)
FIRST = ["base.h"]


def header_order():
    names = [n for n in os.listdir(HEADER_DIR) if n.endswith(".h") and n != LIB + ".h"]
    rest = sorted(n for n in names if n not in FIRST)
    return [n for n in FIRST if n in names] + rest


def summary(lines):
    """The header's opening comment, after the license lines."""
    text = []
    for line in lines[2:]:
        if not line.startswith("//"):
            break
        text.append(line[2:].strip())
    return " ".join(t for t in text if t)


def collect(lines):
    """(doc comment, declaration) pairs for every public function,
    including static inline helpers (shown by their signature)."""
    entries = []
    comment = []
    decl = re.compile(r"^\s*(?:" + cmake_setting("MAUL_MACRO_PREFIX") + r"_API\s+)?[A-Za-z_][A-Za-z0-9_ \*]*\b"
                      + PREFIX + r"[A-Za-z0-9_]+\s*\(")
    depth = 0  # brace depth; declarations live at the extern "C" level
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        code = stripped.split("//")[0]
        if stripped.startswith("///"):
            comment.append(stripped[3:].strip())
            i += 1
            continue
        if (depth <= 1 and decl.match(lines[i]) and not stripped.startswith("typedef")
                and not stripped.startswith("#")):
            sig = stripped
            j = i
            while not re.search(r"[;{]\s*$", sig.split("//")[0].rstrip()) and j < len(lines) - 1:
                j += 1
                sig += " " + lines[j].strip()
            inline = sig.split("//")[0].rstrip().endswith("{")
            sig = re.sub(r"\s+", " ", sig.split("//")[0]).strip()
            sig = re.sub(r"^" + cmake_setting("MAUL_MACRO_PREFIX") + r"_API\s+", "", sig)
            if inline:
                sig = sig[:-1].rstrip() + ";"
                depth += 1
            entries.append((comment, sig))
            comment = []
            i = j + 1
            continue
        depth += code.count("{") - code.count("}")
        if stripped and not stripped.startswith("//"):
            comment = []
        i += 1
    return entries


def main():
    out = [
        "# " + TITLE + " API reference",
        "",
        "Generated from the public headers by `tools/gen_api.py`. The headers",
        "are the source of truth; this file mirrors them.",
        "",
    ]
    total = 0
    headers = header_order()
    for name in headers:
        lines = open(os.path.join(HEADER_DIR, name)).read().split("\n")
        entries = collect(lines)
        if not entries:
            continue
        out += ["## `" + name + "`", "", summary(lines), ""]
        for comment, sig in entries:
            out += ["```c", sig, "```"]
            if comment:
                out.append(" ".join(comment))
            out.append("")
            total += 1
    out += ["---", "", "%d functions across %d headers." % (total, len(headers)), ""]
    dest = os.path.join(ROOT, "docs", "api.md")
    open(dest, "w").write("\n".join(out))
    print("wrote %s (%d functions)" % (dest, total))


if __name__ == "__main__":
    main()
