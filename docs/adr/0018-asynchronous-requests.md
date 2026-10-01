# 0018. Asynchronous requests: an id now, exactly one completion later

Status: Accepted

## Context

Window changes, clipboard reads, file dialogs, GPU device acquisition
and readback finish after the call that starts them, and on the web
nothing may block. Callbacks fire at unsafe points (webgpu.h needed
three callback modes to contain them); requests without a guaranteed
answer leave the caller guessing (winit's resize request may or may
not produce an event).

## Decision

A request returns its status at once; a refusal that needs no platform
round trip is that status alone. An accepted request yields a typed
request id and is answered by exactly one completion record carrying
the id and a typed outcome: done, or a failure from a closed list per
kind that always includes unsupported, denied, superseded and
cancelled. Completions travel in the owner's notification queue in
order with the other notifications; no library calls application code
to deliver them. A wait with a timeout may exist where the platform can
block, and nothing requires it.

## Consequences

Every request pairs with its outcome on every platform, including
changes applied at once and requests a later one replaced. No thread
or re-entrancy rules attach to completions, and a headless test
backend can check the pairing.
