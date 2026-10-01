# 0007. Naming prefixes

Status: Accepted

## Context

Every symbol carries its library's prefix so that all Maul libraries
link into one program without a clash. Two-letter prefixes are crowded:
`ma_` and `MA_` belong to miniaudio, `mu_` and `MU_` to microui.

## Decision

Each library's prefix is `m` plus its short name, at most four
letters, and macros use it in upper case: `m2` and `m3` stay; the new
libraries are `mwin` (Window), `mrhi` (RHI), `mui` (UI), `muni`
(Unicode), `maud` (Audio) and `mnav` (Nav). Windows' `winnls.h`
defines `MUI_*` macros, so public headers are also compiled together
with `windows.h` on Windows.

## Consequences

Names stay short and read as the family's. The Windows check catches a
future macro clash at build time.
