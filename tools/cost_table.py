#!/usr/bin/env python3
# Writes the README's cost table from bench/baseline.txt, so the two
# cannot drift: each row names the baseline rows it shows, and how many
# of its work one core runs in real time (10 ms of audio over its time,
# to two significant figures). Times under 20 us keep one decimal.
#
# Usage: python3 tools/cost_table.py [--check]  (from the repository root)
#   --check  fails, writing nothing, when the README's table differs

import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
README = os.path.join(ROOT, "README.md")
BASELINE = os.path.join(ROOT, "bench", "baseline.txt")
HEAD = "| Work | Time | One core in real time |\n|---|---|---|\n"
BLOCK_US = 10000.0

# The README's label, the baseline rows (a second in parentheses), and
# what one core runs of it: a noun to count, a phrase, or nothing.
ROWS = [
    ("A binaural source, moving", ["binaural.moving.us"], "sources"),
    ("A binaural source, still (with the near field)",
     ["binaural.plain.us", "binaural.still.us"], "sources"),
    ("A source's direct effects (air absorption, a wall's transmission), moving",
     ["direct.moving.us"], "sources"),
    ("A source into the third-order bed", ["bed.encode.us"], "sources"),
    ("The bed decoded binaurally", ["bed.decode3.us"], "=once per listener"),
    ("A reverb", ["reverb.changing.us"], "reverbs"),
    ("Noise suppression, mono (16 kHz, 48 kHz)",
     ["noise.16k.mono.us", "noise.48k.mono.us"], ""),
    ("Echo cancellation, mono (16 kHz, 48 kHz)", ["echo.16k.us", "echo.48k.us"], ""),
]


def baseline():
    figures = {}
    with open(BASELINE, encoding="utf-8") as f:
        for line in f:
            fields = line.split()
            if fields and not fields[0].startswith("#"):
                figures[fields[0]] = float(fields[1])
    return figures


def time_text(us):
    return f"{us:.1f}" if us < 20.0 else f"{us:.0f}"


def count_text(us):
    count = BLOCK_US / us
    digits = 1 - int(math.floor(math.log10(count)))
    return f"{round(count, digits):,.0f}"


def row(label, keys, runs, figures):
    missing = [key for key in keys if key not in figures]
    if missing:
        sys.exit("cost_table: the baseline has no " + ", ".join(missing))
    times = [figures[key] for key in keys]
    if len(times) == 2 and runs != "":
        time = f"{time_text(times[0])} ({time_text(times[1])}) us"
        many = f"about {count_text(times[0])} ({count_text(times[1])}) {runs}"
    else:
        time = ", ".join(time_text(t) for t in times) + " us"
        many = runs[1:] if runs.startswith("=") else (
            f"about {count_text(times[0])} {runs}" if runs else "")
    return f"| {label} | {time} | {many} |" if many else f"| {label} | {time} | |"


def main():
    figures = baseline()
    table = HEAD + "\n".join(row(*spec, figures) for spec in ROWS) + "\n"
    with open(README, encoding="utf-8") as f:
        text = f.read()
    start = text.find(HEAD)
    if start < 0:
        sys.exit("cost_table: README.md has no cost table")
    end = text.find("\n\n", start)
    current = text[start:end + 1]
    if "--check" in sys.argv[1:]:
        if current != table:
            sys.exit("cost_table: README.md's cost table differs from bench/baseline.txt; "
                     "run python3 tools/cost_table.py")
        print("cost table: matches bench/baseline.txt")
        return
    with open(README, "w", encoding="utf-8") as f:
        f.write(text[:start] + table + text[end + 1:])
    print("wrote README.md's cost table")


if __name__ == "__main__":
    main()
