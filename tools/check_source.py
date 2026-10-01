#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The source rules of docs/conventions.md that neither the compiler nor
# clang-format checks:
#
# - every C file, Objective-C (.m) included, starts with its SPDX line;
#   comments are // or ///;
# - comments carry no development history and no TODO or FIXME;
# - no file in the repository contains an em dash;
# - src/ calls no function the family bans (memory goes through the
#   allocator, failures are returned statuses, the library prints
#   nothing, no unsafe string functions, nothing locale-dependent) and
#   none the library bans in tools/source-bans.txt;
# - src/ keeps no thread-local or mutable file-scope state and assigns
#   nothing inside a condition;
# - C file names are snake_case.
#
# Directories listed in tools/external-dirs.txt hold data from outside
# (the Unicode Character Database, for example) and are not checked.
#
# usage: check_source.py

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C_DIRS = ["include", "src", "test", "bench", "samples", "testbed", "tools"]
SKIP_DIRS = {".git", "build", "_site", "out"}
TEXT_SUFFIXES = (".c", ".h", ".m", ".md", ".txt", ".py", ".cmake", ".in", ".yml", ".yaml", ".json")

MARKERS = re.compile(
    r"(?<![\w.#/~])\d+[a-df-z]?-\d+[a-z]?(?![\w.-])"  # slice codes like 2b-9 or 6-3
    r"|\bR\d+-\d+\b|\bS-\d+[a-z]?\b|\btopic-\d+|\bslice \d+|\brev \d+|\btask \d+"
    r"|\bADR-\d+|\bRT\d-[A-Z]+|\bF-T\d+|\bV-[A-Z]{3,}\b|\bD\d\b|\bv\d{2}\b|\bpre-\d+\b"
    r"|(?<![\w{])#\d{3}\b(?!\d)|(?<![\w/.'])[ABDMS]\d{1,2}(?![\w.'])"
    r"|\([FRUW]\d{1,2}\b|\b[FRUW]\d{1,2}'s\b|\b(?:decisions?|records?|items?) [FRUW]\d{1,2}\b"
    r"|\bregistry [A-Z]\d|\b[Pp]hase \d\b|\b[Rr]ound \d+\b"
    r"|\bintegration audit|\baudit [A-Z]\d|\bred[- ]team|\blesson\b|\bledger\b"
)
# The private decision ids, which public Markdown must not cite either.
PRIVATE_IDS = re.compile(
    r"\([FRUW]\d{1,2}\b|\b[FRUW]\d{1,2}'s\b|\b(?:decisions?|records?|items?) [FRUW]\d{1,2}\b"
)
TODO = re.compile(r"\b(TODO|FIXME|XXX)\b")
EM_DASH = "\u2014"

FAMILY_BANS = {
    "malloc": "memory goes through the object's allocator",
    "calloc": "memory goes through the object's allocator",
    "realloc": "memory goes through the object's allocator",
    "free": "memory goes through the object's allocator",
    "aligned_alloc": "memory goes through the object's allocator",
    "assert": "invariants use the library's PP_ASSERT",
    "abort": "a library returns statuses; invariants use PP_ASSERT",
    "exit": "a library never ends the process",
    "printf": "a library prints nothing",
    "fprintf": "a library prints nothing",
    "puts": "a library prints nothing",
    "strcpy": "unbounded string function",
    "strcat": "unbounded string function",
    "sprintf": "unbounded string function",
    "vsprintf": "unbounded string function",
    "gets": "unbounded string function",
    "atoi": "no error reporting",
    "atol": "no error reporting",
    "atof": "locale-dependent and no error reporting",
    "strtod": "locale-dependent",
    "setlocale": "locale-dependent",
    "isalpha": "locale-dependent",
    "isdigit": "locale-dependent",
    "isspace": "locale-dependent",
    "isalnum": "locale-dependent",
    "isupper": "locale-dependent",
    "islower": "locale-dependent",
    "toupper": "locale-dependent",
    "tolower": "locale-dependent",
}
# The one place a zeroed allocator reaches the C library.
ALLOCATOR_FILE = "allocator.c"
ALLOCATOR_CALLS = {"malloc", "calloc", "realloc", "free", "aligned_alloc"}

THREAD_LOCAL = re.compile(r"\b(_Thread_local|thread_local|__thread)\b|__declspec\s*\(\s*thread\s*\)")
FILE_SCOPE_STATE = re.compile(r"^static\s+(?!const\b)(?!inline\b)(?![^(=;]*\()[^=;]*[=;]")
ASSIGN_IN_CONDITION = re.compile(
    r"\b(?:if|while)\s*\((?:[^()]|\([^()]*\))*?[^=!<>+\-*/%&|^]=(?!=)")
SNAKE_CASE = re.compile(r"^[a-z0-9_]+\.[chm]$")


def code_text(line):
    """A C line without its // comment and its string and char literals."""
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    line = re.sub(r"'(?:[^'\\]|\\.)*'", "''", line)
    at = line.find("//")
    return line if at < 0 else line[:at]


def comment_text(line):
    """The // comment part of a C line, or None."""
    code = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    at = code.find("//")
    return None if at < 0 else line[at:]


def library_bans():
    """The library's own bans: one 'name reason' pair per line."""
    bans = {}
    path = os.path.join(ROOT, "tools", "source-bans.txt")
    if not os.path.exists(path):
        return bans
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.match(r"(\w+)\s+(.+)$", line)
        if not match:
            sys.exit(f"source-bans.txt:{number}: expected 'name reason'")
        bans[match.group(1)] = match.group(2)
    return bans


def external_dirs():
    """Directories, relative to the root, whose files come from outside."""
    path = os.path.join(ROOT, "tools", "external-dirs.txt")
    if not os.path.exists(path):
        return set()
    result = set()
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            result.add(os.path.normpath(line))
    return result


EXTERNAL = external_dirs()


def walk(top, suffixes):
    for folder, dirs, files in os.walk(os.path.join(ROOT, top)):
        if os.path.relpath(folder, ROOT) in EXTERNAL:
            dirs[:] = []
            continue
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for name in sorted(files):
            if name.endswith(suffixes):
                yield os.path.join(folder, name)


def check_c_file(path, rel, in_src, bans, findings):
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    if not lines[0].startswith("// SPDX-License-Identifier: MIT"):
        findings.append(f"{rel}:1: the file does not start with its SPDX line")
    if not SNAKE_CASE.match(os.path.basename(path)):
        findings.append(f"{rel}: file names are snake_case")
    for number, line in enumerate(lines, 1):
        if "/*" in code_text(line):
            findings.append(f"{rel}:{number}: use // comments, not /* */")
        comment = comment_text(line)
        if comment is not None:
            if MARKERS.search(comment):
                findings.append(f"{rel}:{number}: development-history marker: {line.strip()}")
            if TODO.search(comment):
                findings.append(f"{rel}:{number}: TODO or FIXME; open an issue instead")
        if not in_src:
            continue
        code = code_text(line)
        for name in re.findall(r"(?<![\w.>])([A-Za-z_]\w*)\s*\(", code):
            if name in ALLOCATOR_CALLS and os.path.basename(path) == ALLOCATOR_FILE:
                continue
            reason = bans.get(name)
            if reason:
                findings.append(f"{rel}:{number}: banned call {name}(): {reason}")
        if THREAD_LOCAL.search(code):
            findings.append(f"{rel}:{number}: no thread-local state; failures are returned")
        if FILE_SCOPE_STATE.match(code):
            findings.append(f"{rel}:{number}: mutable file-scope state: {line.strip()}")
        if ASSIGN_IN_CONDITION.search(code):
            findings.append(f"{rel}:{number}: assignment inside a condition: {line.strip()}")


def main():
    findings = []
    bans = dict(FAMILY_BANS)
    bans.update(library_bans())
    for top in C_DIRS:
        for path in walk(top, (".c", ".h", ".m")):
            rel = os.path.relpath(path, ROOT)
            check_c_file(path, rel, rel.startswith("src" + os.sep), bans, findings)
    for path in walk(".", TEXT_SUFFIXES):
        rel = os.path.relpath(path, ROOT)
        for number, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
            if EM_DASH in line:
                findings.append(f"{rel}:{number}: em dash")
            if path.endswith(".md") and PRIVATE_IDS.search(line):
                findings.append(f"{rel}:{number}: private decision id: {line.strip()}")
    for finding in findings:
        print(finding)
    if findings:
        print(f"{len(findings)} finding(s); see docs/conventions.md")
        return 1
    print("source rules: all files pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
