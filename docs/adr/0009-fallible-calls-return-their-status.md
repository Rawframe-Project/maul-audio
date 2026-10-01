# 0009. A fallible call returns its status

Status: Accepted

## Context

Record 0002 recorded refusals in a thread-local slot beside a null
result. A reason kept apart from the call can be stale, can be read on
the wrong thread when a binding resumes on another one, and conflicts
with libraries that must keep no hidden state.

## Decision

Every function that can fail returns a status from its library's
closed result enum, marked with the library's nodiscard macro; its
results go through out-parameters. Where the position of a failure
matters (validation, decoding, parsing) it returns a small struct with
the status and the byte offset. No library keeps a failure reason in
thread-local or global state. Invalid input against a live owner object
still counts one misuse on that object. Each library provides a
function that names a status.

## Consequences

Failures cannot be missed or misattributed. Maul2D and Maul3D change
their creation functions to this shape before 1.0. Record 0002 is
superseded by this one.
