#!/usr/bin/env python3
# Builds the documentation site: docs/*.md and CHANGELOG.md become
# _site/*.html inside one shared page shell. The markdown stays plain
# for the repository view; this script owns the site view. The same
# script serves every Maul library; it reads the library's name and
# title from CMakeLists.txt.
# Requires: pip install markdown.
#
# Usage: python3 tools/build_docs.py  (from the repository root)

import pathlib
import re

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
OUT = ROOT / "_site"


def cmake_setting(name):
    """A value the library's CMakeLists.txt sets with set(NAME value)."""
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"^set\(" + name + r" \"?([^\")]+)\"?\)", text, re.M)
    if not match:
        raise SystemExit("CMakeLists.txt does not set " + name)
    return match.group(1)


LIB = cmake_setting("MAUL_LIBRARY")
TITLE = cmake_setting("MAUL_TITLE")

PAGES = [
    ("index", TITLE, DOCS / "index.md"),
    ("guide", "Guide", DOCS / "guide.md"),
    ("api", "API", DOCS / "api.md"),
    ("samples", "Samples", DOCS / "samples.md"),
    ("conventions", "Conventions", DOCS / "conventions.md"),
    ("changelog", "Changelog", ROOT / "CHANGELOG.md"),
]

SHELL = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} | {site}</title>
<style>
:root {{ color-scheme: light dark; }}
body {{ max-width: 46rem; margin: 0 auto; padding: 1rem 1.25rem 4rem;
       font: 16px/1.6 system-ui, sans-serif; }}
nav {{ padding: 0.75rem 0; border-bottom: 1px solid #8884; margin-bottom: 1.5rem; }}
nav a {{ margin-right: 1.25rem; text-decoration: none; font-weight: 600; }}
pre {{ overflow-x: auto; padding: 0.75rem; border: 1px solid #8884; border-radius: 6px; }}
code {{ font-size: 0.92em; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #8886; padding: 0.3rem 0.6rem; }}
h1, h2 {{ line-height: 1.25; }}
</style>
</head>
<body>
<nav>{nav}<a href="https://github.com/Rawframe-Project/{lib}">GitHub</a></nav>
{body}
</body>
</html>
"""


def main():
    OUT.mkdir(exist_ok=True)
    pages = [p for p in PAGES if p[2].exists()]
    nav = "".join('<a href="%s.html">%s</a>' % (name, title) for name, title, _ in pages)
    md = markdown.Markdown(extensions=["fenced_code", "tables", "toc"])
    for name, title, source in pages:
        body = md.reset().convert(source.read_text(encoding="utf-8"))
        # Links between the markdown files point at .md; the site serves .html.
        body = body.replace('.md"', '.html"').replace("../CHANGELOG.html", "changelog.html")
        page = SHELL.format(title=title, site=TITLE, nav=nav, lib=LIB, body=body)
        (OUT / (name + ".html")).write_text(page, encoding="utf-8")
        print("built", name + ".html")


if __name__ == "__main__":
    main()
