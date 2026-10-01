# 0013. Performance discipline

Status: Accepted

## Context

The libraries must be fast and small, and a regression nobody measures
goes unnoticed. Gating CI on timings or instruction counts would tie the
build to one environment.

## Decision

Each library has plain C benchmarks with fixed inputs that print their
timings. A change meant to affect performance carries before-and-after
numbers from one machine in its commit message. CI reports the
library's size in total and per optional feature; size budgets are
checked when a release is made. Hot paths take no locks, make no system
calls and call nothing locale-dependent.

## Consequences

Performance is measured by the people who change it, without
environment-bound machinery in CI.
