# 0016. Roots are pointers, what they own are generation-checked ids

Status: Accepted

## Context

A library names objects across calls. Some can disappear under the
caller: a window closes, a monitor unplugs, a gamepad disconnects, a
GPU resource is destroyed on another path. The root object a caller
creates first and destroys last cannot. Pointers to vanished objects
dangle (GLFW); validating pointers takes a process-wide table (SDL3).

## Decision

A library's root object is a typed opaque pointer created with the
caller's allocator. Every object a root owns is named by a typed value
id, a struct of a 1-based slot index (0 is null) and a generation, one
struct per kind. A stale id is refused with a typed result, never
dereferenced; a replugged device or a restored surface is a new id or
generation. Platform handles are never identities and cross only in
typed bundles a library defines for that purpose.

## Consequences

Using a vanished object costs one array read and a compare and is a
refusal, not a crash. Ids are plain data that events, other threads
and saved state can carry. No library keeps a global table of objects.
Maul2D and Maul3D keep their released world ids until a major version
has another reason to change.
