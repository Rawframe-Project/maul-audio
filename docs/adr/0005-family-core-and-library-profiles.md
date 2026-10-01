# 0005. Family core and library profiles

Status: Accepted

## Context

The family grew from two physics engines to libraries for windows,
GPU devices, UI, Unicode, audio and navigation. The rulebook was
written for physics: its determinism, memory and threading sections
speak of worlds and steps, which most new libraries do not have. Writing
every library's exceptions into one file would turn it into a list of
special cases.

## Decision

`docs/conventions.md` holds only the rules every library follows, and
is identical in every repository. What depends on a library's domain
(its determinism class, its thread exceptions, its memory model, its
platform dependencies and its commit areas) is stated in one design
record per library, its profile. A profile may add to the rulebook but
never loosen it.

## Consequences

A reader learns the family rules once. Each library states plainly
what its domain adds. Maul2D and Maul3D move their determinism, memory
and threading sections into their profiles without changing them.
