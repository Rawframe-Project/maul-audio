# 0010. Allocators are per object

Status: Accepted

## Context

A process-wide allocator hook must be set before first use and never
changed, and gives a process one allocator per library. Two components
of one program cannot use the same library with different allocators.

## Decision

No library has a process-wide allocator hook. Every owner object takes
an allocator in its def: an `alloc(size, alignment, context)` and a
`free(memory, size, alignment, context)` function and a context, with
no realloc; a zeroed allocator means the C library's. A function
without an owner object does not allocate: it works in caller memory
and reports the size it needs when the buffer is too small. Hot paths
never allocate, and running out of memory is a returned status.

## Consequences

Hosts can route each object to its own arena or pool. Maul2D and
Maul3D move their allocator into their world defs before 1.0.
