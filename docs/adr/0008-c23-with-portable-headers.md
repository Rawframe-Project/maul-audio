# 0008. C23 implementations, portable headers

Status: Accepted

## Context

C23 adds checked integer arithmetic, `nullptr`, `constexpr` objects,
fixed-width enumerations and standard attributes. GCC 14 and Clang 19
implement it; MSVC's C compiler implements little of it. Public headers
are compiled by every consumer's compiler, including C17 programs and
MSVC's C++ compiler.

## Decision

Library code is C23 and uses only features the compiler provides
(`<stdbit.h>`, which comes from the C library, is not used). Public
headers are written in the common subset of C17 and C++17, which is
also valid C23; attributes newer dialects need sit behind library
macros. A test compiles every public header alone as C17, C23 and
C++17 with pedantic warnings as errors, and Windows CI compiles each
again with MSVC's `cl.exe` as C17 and C++17 (`cmake/HeaderCheck`; MSVC
has no C23 mode). A nodiscard macro reads `_MSVC_LANG` as well as
`__cplusplus`, which MSVC keeps at 199711L unless `/Zc:__cplusplus` is
given, and the header test checks that it is not empty in C++17. The
minimum compilers are GCC 14 and Clang 19; on Windows the compiler is
`clang-cl`.

## Consequences

Maintainers write modern C while every consumer can include the
headers. Building the sources with MSVC's `cl.exe` is not supported;
`clang-cl`, which Visual Studio installs, produces compatible objects.
