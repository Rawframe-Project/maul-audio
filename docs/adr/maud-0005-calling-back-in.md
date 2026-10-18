# maud-0005. Host code that calls back in is refused and counted

Status: Accepted

## Context

The library calls host code: the stream callback, every owner object's
allocator, and the spatializer's ray queries and task hooks. Conventions
section 10 says such code calls no function of the object it serves
unless the profile allows it, that the object refuses such a call with
the state error and counts one misuse, and that a callback that must
call back in has a named list. Before 1.1.0 the stream callback's
control calls were refused (by the thread marked rendering), but the
context's allocator and the spatializer's hooks and allocator could
call back in unchecked, into a half-made stream slot or a step's own
sources and probe sets.

## Decision

- The context records the thread on which it runs its allocator. Every
  call it refuses on a rendering thread is also refused on that thread
  with `maud_errorState` and counted as misuse, and a stream rendered
  again from its own callback is counted too.
- `maudStreamCallback` may call the stream queries
  (`maudGetStreamFormat`, `maudGetStreamPosition`,
  `maudGetStreamClock`), `maudGetHostNanoseconds`,
  `maudGetContextMisuse`, and render or feed another offline stream.
- The spatializer holds a flag while it runs host code (its ray
  queries, task hooks and allocator). Every function of its simulation
  side that changes it refuses a call with `maud_errorState` while the
  flag is set and counts one misuse; `maudDestroySpatializer` does
  nothing then. The hooks may call the rendering side
  (`maudLatchResults`, `maudGetDirectResult`, `maudGetReverbResult`,
  `maudRenderReflections`) and `maudGetSpatializerMisuse`.
- `maudGetSpatializerMisuse` counts those refusals and invalid
  arguments to the functions that change the spatializer, as
  `maudGetContextMisuse` does for the context (conventions section 8);
  stale ids and malformed bake files are not misuse.
- Every other object calls its allocator only while it is made or
  destroyed, when the host holds no valid handle to it.

## Consequences

A host whose callback or allocator reaches back into the object gets a
status and a count instead of corrupted state. The marker follows where
host code runs: the spatializer's hooks may run on any of the host's
task threads, so only an object-wide flag sees them, and its simulation
side is used by one thread at a time, so the flag refuses nothing
legitimate. The context's allocator runs on the calling thread while
another thread may legitimately render, so its marker is that thread.
A check is one atomic load per call, so `maudSetSourcePose`, called
thousands of times a frame, costs what it did; an exchange on every
call would have caught races too, at a read-modify-write each.
