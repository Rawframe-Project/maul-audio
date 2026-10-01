#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The module graph rule of docs/conventions.md, section 6. Every module
# in src/ (a .c or .m file and its header, or a header alone) is declared in
# tools/modules.txt with the modules it may use:
#
#     tables:
#     properties: tables
#     segment: properties utf8
#
# A module may include its own header, any public header and the headers
# of the modules its line names, nothing else. The declared graph has no
# cycles, and names no module that does not exist.
#
# usage: check_modules.py

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"/]+)\.h"')


def declared():
    graph = {}
    path = os.path.join(ROOT, "tools", "modules.txt")
    if not os.path.exists(path):
        sys.exit("tools/modules.txt is missing")
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        match = re.match(r"([a-z0-9_]+)\s*:\s*([a-z0-9_ ]*)$", line)
        if not match:
            sys.exit(f"modules.txt:{number}: expected 'module: dependency dependency'")
        if match.group(1) in graph:
            sys.exit(f"modules.txt:{number}: {match.group(1)} is declared twice")
        graph[match.group(1)] = set(match.group(2).split())
    return graph


def modules_in_src():
    src = os.path.join(ROOT, "src")
    found = {}
    for name in sorted(os.listdir(src)):
        stem, suffix = os.path.splitext(name)
        if suffix in (".c", ".h", ".m"):
            found.setdefault(stem, []).append(name)
    return found


def cycle(graph):
    """One cycle in the graph as a list of modules, or None."""
    state = {}

    def visit(node, path):
        state[node] = "open"
        for dep in sorted(graph.get(node, ())):
            if state.get(dep) == "open":
                return path[path.index(dep):] + [dep]
            if dep not in state:
                found = visit(dep, path + [dep])
                if found:
                    return found
        state[node] = "done"
        return None

    for node in sorted(graph):
        if node not in state:
            found = visit(node, [node])
            if found:
                return found
    return None


def main():
    graph = declared()
    found = modules_in_src()
    errors = []
    for module in sorted(set(found) - set(graph)):
        errors.append(f"src/{module}: module not declared in tools/modules.txt")
    for module in sorted(set(graph) - set(found)):
        errors.append(f"modules.txt: {module} has no file in src/")
    for module, deps in sorted(graph.items()):
        for dep in sorted(deps - set(graph)):
            errors.append(f"modules.txt: {module} uses {dep}, which is not declared")
    loop = cycle(graph)
    if loop:
        errors.append("modules.txt: cycle " + " -> ".join(loop))
    for module, files in sorted(found.items()):
        allowed = graph.get(module, set()) | {module}
        for name in files:
            path = os.path.join(ROOT, "src", name)
            for number, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                match = INCLUDE.match(line)
                if match and match.group(1) not in allowed:
                    errors.append(f"src/{name}:{number}: {module} may not include "
                                  f"{match.group(1)}.h; declare the edge or remove the include")
    for error in errors:
        print(error)
    if errors:
        return 1
    print(f"module graph: {len(graph)} modules, every include allowed, no cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
