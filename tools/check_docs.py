#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The documentation contract of docs/conventions.md, section 7. Every
# function a public header declares with the library's API macro has a
# /// block directly above it with:
#
# - a summary sentence as its first line;
# - @param for each named parameter, and none for a parameter that does
#   not exist;
# - @return, unless the function returns void;
# - a "@par Thread safety" paragraph that opens with one of the
#   statements of section 10 (THREAD_SAFETY below).
#
# And every design record of the library's own that a tracked file cites
# (`P-NNNN`) exists in docs/adr/, each listed in the library's index,
# docs/adr/P.md.
#
# usage: check_docs.py

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The openings a thread safety paragraph may have (section 10).
THREAD_SAFETY = [
    re.compile(r"Safe from any thread\.(\s|$)"),
    re.compile(r"Safe from any thread; [^.;]+ (is|are) used by one thread at a time[.,;]"),
    re.compile(r"Main thread only\.(\s|$)"),
    re.compile(r"Safe from any thread; it runs on the main thread, waiting at most the marshal "
               r"deadline\.(\s|$)"),
    re.compile(r"Real-time safe: no allocation, lock or wait\.(\s|$)"),
]


def cmake_setting(name):
    """A value the library's CMakeLists.txt sets with set(NAME value)."""
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        match = re.search(r"^set\(" + name + r" \"?([^\")]+)\"?\)", f.read(), re.M)
    if not match:
        raise SystemExit("CMakeLists.txt does not set " + name)
    return match.group(1)


def split_top_level(text):
    """Splits a parameter list at the commas outside parentheses."""
    parts, depth, current = [], 0, ""
    for char in text:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += char
    parts.append(current)
    return [part.strip() for part in parts if part.strip()]


def parameter_name(parameter):
    pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)", parameter)
    if pointer:
        return pointer.group(1)
    names = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[[^\]]*\]", "", parameter))
    return names[-1] if names else None


def declarations(lines, macro):
    """Yields (line number, doc lines, declaration text) for each API function."""
    start_pattern = re.compile(r"^\s*(?:" + macro + r"_NODISCARD\s+)?" + macro + r"_API\b")
    i = 0
    while i < len(lines):
        if start_pattern.match(lines[i]):
            first = i
            text = lines[i]
            while ";" not in text and i + 1 < len(lines):
                i += 1
                text += " " + lines[i].strip()
            doc = []
            j = first - 1
            while j >= 0 and lines[j].strip().startswith("///"):
                doc.insert(0, lines[j].strip()[3:].strip())
                j -= 1
            yield first + 1, doc, text
        i += 1


def check(rel, number, doc, text, macro, errors):
    match = re.search(r"(\w+)\s*\((.*)\)\s*;", text)
    if not match:
        errors.append(f"{rel}:{number}: cannot read the declaration")
        return
    name, params = match.group(1), match.group(2)
    head = text[:match.start(1)]
    head = re.sub(r"\b" + macro + r"_(API|NODISCARD)\b", "", head).strip()
    where = f"{rel}:{number}: {name}"
    if not doc:
        errors.append(f"{where}: no /// documentation")
        return
    if not doc[0] or doc[0].startswith("@"):
        errors.append(f"{where}: the first /// line is not a summary sentence")
    names = [] if params.strip() in ("", "void") else [
        parameter_name(p) for p in split_top_level(params)]
    documented = [m.group(1) for line in doc for m in [re.match(r"@param(?:\[\w+\])?\s+(\w+)", line)]
                  if m]
    for param in names:
        if param and param not in documented:
            errors.append(f"{where}: no @param for {param}")
    for param in documented:
        if param not in names:
            errors.append(f"{where}: @param {param} names no parameter")
    returns_void = re.fullmatch(r"(?:static\s+)?(?:inline\s+)?void", head) is not None
    has_return = any(line.startswith("@return") for line in doc)
    if not returns_void and not has_return:
        errors.append(f"{where}: no @return")
    if returns_void and has_return:
        errors.append(f"{where}: @return on a void function")
    starts = [i for i, line in enumerate(doc) if line.startswith("@par Thread safety")]
    if not starts:
        errors.append(f"{where}: no '@par Thread safety' paragraph")
        return
    paragraph = []
    for line in doc[starts[0] + 1:]:
        if not line or line.startswith("@"):
            break
        paragraph.append(line)
    text = " ".join(paragraph)
    if not any(pattern.match(text) for pattern in THREAD_SAFETY):
        errors.append(f"{where}: the thread safety paragraph opens with no statement of "
                      "section 10")


def check_records(prefix, errors):
    """Cited records exist and the index lists every record."""
    folder = os.path.join(ROOT, "docs", "adr")
    records = {}
    for name in os.listdir(folder):
        match = re.match(r"(" + prefix + r"-\d{4})-.*\.md$", name)
        if match:
            records[match.group(1)] = name
    with open(os.path.join(folder, prefix + ".md"), encoding="utf-8") as f:
        index = f.read()
    for record, name in sorted(records.items()):
        if f"({name})" not in index:
            errors.append(f"docs/adr/{prefix}.md: {record} is not listed")
    tracked = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True,
                             check=True).stdout.decode("utf-8").split("\0")
    cited = re.compile(r"\b(" + prefix + r"-\d{4})\b")
    for rel in sorted(path for path in tracked if path):
        try:
            with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
                text = f.read()
        except (UnicodeDecodeError, FileNotFoundError):
            continue
        for record in sorted(set(cited.findall(text))):
            if record not in records:
                errors.append(f"{rel}: cites {record}, which docs/adr/ does not have")
    return len(records)


def main():
    lib = cmake_setting("MAUL_LIBRARY")
    macro = cmake_setting("MAUL_MACRO_PREFIX")
    folder = os.path.join(ROOT, "include", lib)
    errors = []
    count = 0
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".h"):
            continue
        rel = f"include/{lib}/{name}"
        lines = open(os.path.join(folder, name), encoding="utf-8").read().split("\n")
        for number, doc, text in declarations(lines, macro):
            count += 1
            check(rel, number, doc, text, macro, errors)
    records = check_records(cmake_setting("MAUL_API_PREFIX"), errors)
    for error in errors:
        print(error)
    if errors:
        print(f"{len(errors)} finding(s) in {count} public functions and {records} records; "
              "see docs/conventions.md")
        return 1
    print(f"documentation: all {count} public functions documented, {records} records found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
