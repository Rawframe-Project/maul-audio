# 0017. No threads of the library's own, platform threads by contract, typed thread safety

Status: Accepted

## Context

Libraries in the references start threads for convenience (SDL3's
event, joystick and rumble threads, winit's pump thread), each with
alternatives the pump already offers. Some threads belong to the
platform and cannot be refused: the audio callback, platform callbacks
on arbitrary threads. Thread affinity written as free text is vague
and cannot be checked.

## Decision

A library starts no thread unless a platform leaves no other way to
meet a requirement; such a thread is declared in the library profile,
started by the root or an object it owns only while needed, joined
by that object's stop or destroy and never later than the root's
destroy, and never runs application code. On a thread the platform owns, a library only
publishes into bounded queues or reads what was published: no
allocation, lock or wait, and no application code except a callback
the profile declares real-time. Every public function's thread safety
paragraph opens with one statement from the closed list in
`conventions.md` section 10, which `tools/check_docs.py` enforces.

## Consequences

A host sees every thread a library may run and when it ends. Thread
affinity is greppable and checked, and the API reference can tabulate
it.

## Amendment, 2026-10-01

A declared thread's lifetime was "started by the root's create, joined
by its destroy", which keeps a thread for an object that may never be
used (an audio stream's thread with no stream open). It now lives while
the object that needs it does, and is still joined by the root's
destroy at the latest.
