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
started by the root's create, joined by its destroy, and never runs
application code. On a thread the platform owns, a library only
publishes into bounded queues or reads what was published: no
allocation, lock or wait, and no application code except a callback
the profile declares real-time. Every public function's thread safety
paragraph opens with one statement from the closed list in
`conventions.md` section 10, which `tools/check_docs.py` enforces.

## Consequences

A host sees every thread a library may run and when it ends. Thread
affinity is greppable and checked, and the API reference can tabulate
it.
