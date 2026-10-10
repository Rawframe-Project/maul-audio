# Maul conventions

These rules govern every Maul library. The file is identical in every
Maul repository; its canonical copy lives in the family's docs
repository and changes land in every library together. New code
follows every rule. Where older code does not yet, the gap is a bug to
fix, not a precedent to follow.

What depends on a library's domain (its determinism class, its thread
exceptions, its memory model, its commit areas) is not here. Each
library states it in its profile, a design record described in
section 18.

Every rule names what checks it. A rule marked **review** has no tool;
reviewers check it.

In the rules below, `P` stands for the library prefix (`m2`, `m3`,
`mwin`, `mrhi`, `mui`, `muni`, `maud`, `mnav`), `PP` for its macro
prefix (`M2`, `MWIN`, ...) and `lib` for the library name (`maul2d`,
`maul-unicode`, ...).

## 1. Language

- Everything in the repository is written in English: code, comments,
  documentation, commit messages, issues and pull requests.
- Spelling is American English (`behavior`, `normalize`, `color`).
- Write plainly. Say what a thing does and why. No slogans, no
  metaphors that need decoding, no editorial asides about other
  projects.

Checked by: review.

## 2. Repository layout

| Path | Contents |
|---|---|
| `include/lib/` | Public headers, the only API. |
| `src/` | Implementation and internal headers. |
| `test/` | Test suites, one executable per suite. |
| `bench/` | Benchmarks. |
| `samples/` | Programs that use the library as a host does: standalone ones built against the installed package, and ones built in the tree that run as tests. |
| `testbed/` | Interactive or visual tools. Not part of the library. |
| `tools/` | Developer scripts, command-line tools and generators. |
| `cmake/` | CMake modules and package templates. |
| `docs/` | Guides, the API reference and design records (`docs/adr/`). |
| `.github/` | CI workflows. |

Root files are `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`,
`LICENSE`, `CMakeLists.txt` and the tool configuration files
(`.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`,
`.gitignore`). A directory a library does not need is left out.

Checked by: review.

## 3. Language standard

- Library code is C23. It uses only C23 features the compiler provides.
  `<stdbit.h>`, which comes from the C library and is missing on some
  targets, is not used; bit operations go through the family's builtin
  header.
- Public headers are written in the common subset of C17 and C++17,
  which is also valid C23. Attributes only newer dialects accept sit
  behind the library's macros (`PP_NODISCARD`). An enumeration whose
  size matters in a public struct is a `typedef` of a fixed-width
  integer with named constants.
- Minimum compilers: GCC 14 and Clang 19 (GCC 15 where a library uses
  `#embed`). On Windows the compiler is `clang-cl`.

Checked by: the build (`C_STANDARD 23`); the header test, which
compiles every public header as C17, C23 and C++17 with warnings as
errors.

## 4. Naming

### C code

| Kind | Form | Example |
|---|---|---|
| Public type | `P` + PascalCase | `m3BodyDef`, `muniUtf8Result` |
| Public function on an object | `P` + Object `_` + Verb[Noun] | `m3Body_GetPosition` |
| Public lifetime function | `P` + `Create`/`Destroy` + Object | `m3CreateWorld` |
| Public free function | `P` + Verb[Noun] | `muniValidateUtf8`, `m3Hash64` |
| Def initializer | `P` + `Default` + Type | `m3DefaultBodyDef` |
| Enum type | `P` + PascalCase | `m3BodyType` |
| Enum value | `P` + `_` + camelCase | `m3_dynamicBody`, `muni_errorInvalid` |
| Macro, constant | `PP_` + UPPER_SNAKE | `M3_MAX_WORLDS` |
| Internal function shared across files | `P` + PascalCase, no `_` | `m3PrepareContacts` |
| `static` function | PascalCase | `UpdatePairs` |
| Local variable, parameter, struct field | camelCase | `bodyIndex`, `pairCount` |
| Out-parameter | camelCase ending in `Out` | `windowOut`, `neededOut` |
| File-scope `static` variable | `s_` + camelCase | `s_propertyTable` |

- Verbs are consistent across the family: `Get`, `Set`, `Is`, `Has`,
  `Enable`, `Disable`, `Create`, `Destroy`, `Apply`, `Cast`,
  `Overlap`, `Collide`, `Validate`, `Next`.
- Acronyms are written as words: `Aabb`, `Utf8`, `Id`, `Ccd`.
- Math functions take their mathematical names (`Atan2`, `Hash64`).
- A field, parameter or out-parameter whose type is a typed id is
  named for what it names, without `Id`: `owner`, `window`,
  `windowOut`. Maul2D and Maul3D keep the `Id` names their released
  API holds (`bodyId`, `shapeIdA`).
- Create functions take their owner first, then the def, then any
  geometry, and end with the out-parameter for the new id.
- The same concept has the same name in every library. When one
  library gains a function another already has, it takes the existing
  name.

Checked by: review. `clang-tidy`'s identifier naming check covers the
case of locals, parameters and fields.

### Files and directories

- C sources and headers: `snake_case.c`, `snake_case.h`.
- Test suites: `test/test_<area>.c`; test functions `Test<Behavior>`.
- Markdown in `docs/`: `kebab-case.md`. Design records:
  `docs/adr/NNNN-kebab-case-title.md`.
- Directories: lowercase, no separators where one word suffices.
- Git branches: `kebab-case`, prefixed with the commit area when
  useful (`bidi-bracket-pairs`).

Checked by: `tools/check_source.py`.

### CMake

- Options: `LIB_...` in upper snake case, named for what they switch
  (`MAUL3D_BUILD_TESTS`, `MAUL_UNICODE_SANITIZE`).
- Functions and macros shared by the family: `maul_snake_case`.
  Library-specific ones: `lib_snake_case`.
- Targets: the library is `lib`, exported as `lib::lib`; tests are
  `test_<area>`; tools are `P<tool>` (`m3replay`, `munigen`).

Checked by: review.

## 5. Formatting

- `clang-format` with the family's `.clang-format`, at the version CI
  pins. Run it; do not argue with it.
- 4 spaces, no tabs, 100 columns, Allman braces, braces on every `if`,
  `else`, `for`, `while` and `do` (`InsertBraces`), `const` on the left
  (`QualifierAlignment: Left`), includes grouped and sorted
  (`IncludeBlocks: Regroup`).
- One declaration per line. Declare variables at first use.
- Files end with one newline, use LF line endings and UTF-8 without a
  byte order mark.

Checked by: `clang-format --dry-run -Werror` in CI; `.editorconfig`
and `.gitattributes`.

## 6. Files and modules

- A module is one `.c` file with one internal header of the same name
  in `src/`. The header declares exactly what other files may call.
- Each library declares its module graph in `tools/modules.txt`: every
  module and the modules it may use. No module may include the header
  of a module the graph does not allow, and the graph has no cycles.
- A part built as a library of its own from its own sources (a
  renderer on another Maul library, say) lives in a directory beside
  `src/`, its sources in `DIR/src` and its public headers in
  `DIR/include`, and is listed in `tools/source-dirs.txt`. The rules
  for `src/` hold there too, and its sources include their own headers
  and public ones alone, never the library's internals. The API
  reference gives each part's headers a section of their own.
- Every `.c` file includes its own header first, then other internal
  headers, then public headers (`"lib/x.h"`), then system headers
  (`<...>`), one blank line between groups, each group sorted.
- Every function with external linkage is declared in a header.
- Header guards follow the path: public headers use `LIB_NAME_H`,
  internal headers `LIB_SRC_NAME_H`.
- Public headers declare only the API. Nothing internal appears in
  `include/`.
- A source file stays under 1000 lines and a function under 80 lines.
  A longer one is listed in `tools/length-exceptions.txt` with a ceiling
  and the reason it is kept whole. The list only shrinks: nothing may
  grow past its ceiling, and an entry that is back under its limit is
  deleted.
- A function's cognitive complexity stays at or under 25. A function
  kept above it carries
  `// NOLINT(readability-function-cognitive-complexity): <reason>` on
  the line that opens it, and `tools/check_lengths.py --structs` lists
  every such exception.
- A struct holds one concept. An owner struct composes subsystem
  structs instead of collecting loose fields.
- No global mutable state. Constant tables are immutable.
- Generated sources live in `src/generated/`, written by a generator in
  `tools/`. Each starts with its SPDX line, names the generator and its
  inputs, and turns clang-format off. They are data: the length,
  complexity and clang-tidy rules do not apply to them, and they are
  never edited by hand. CI regenerates them and fails on any
  difference.

Checked by: `tools/check_modules.py` (graph); `clang-format`
(include order); `-Wmissing-prototypes`; `tools/check_lengths.py`
(lines); `clang-tidy` `readability-function-cognitive-complexity`
(complexity); review, with the field-count report of
`tools/check_lengths.py` (structs); `tools/check_source.py` (mutable
file-scope state).

## 7. Comments and documentation in code

- Comments explain why, and what is not obvious from the code. They do
  not narrate the code line by line.
- Full sentences, capitalized, ending with a period. `//` comments
  only; public API documentation uses `///` directly above the
  declaration.
- Every source file starts with its SPDX line and copyright:
  `// SPDX-License-Identifier: MIT`.
- Every public declaration is documented with Doxygen commands: a
  summary sentence, `@param` for each parameter, `@return` unless it
  returns nothing (naming the statuses it can return), and a
  `@par Thread safety` paragraph.
- No development history in the source: no ticket or task numbers,
  review round names, phase or slice labels, dates, version stamps,
  "was/used to" stories or changelogs. History lives in git and in
  `CHANGELOG.md`; design reasoning lives in `docs/adr/`.
- A design record cited as `P-NNNN` exists in `docs/adr/` and is
  listed in the library's index of records, `docs/adr/P.md`.
- No `TODO` or `FIXME` comments. Open an issue instead.
- No comparisons with other libraries in code comments.
- No em dash character anywhere in the repository.

```c
/// Validates UTF-8 as hostile input.
///
/// @param bytes   The bytes to check. May be NULL when length is 0.
/// @param length  The number of bytes.
/// @return `muni_success`, or the first error and its byte offset.
/// @par Thread safety
/// Safe from any thread.
MUNI_NODISCARD muniUtf8Result muniValidateUtf8(const char* bytes, size_t length);
```

Checked by: `tools/check_docs.py` (public documentation, records);
`tools/check_source.py` (SPDX line, history markers, `TODO`, `FIXME`,
em dash, `/* */` comments); review (content).

## 8. Errors and assertions

- A function that can fail returns a status from its library's closed
  result enum, marked `PP_NODISCARD`, which comes first in the
  declaration (`PP_NODISCARD PP_API ...`) because C++ accepts a standard
  attribute only there. What it produces goes through
  out-parameters. Where the position of a failure matters (validation,
  decoding, parsing of external bytes) it returns a small result struct
  holding the status and the byte offset.
- Status codes are closed enums with success as zero. Each library
  provides `PResultName(result)`, which returns a static string.
- No library keeps a failure reason in thread-local or global state.
- Invalid input against a live owner object (a world, a device, a
  context) counts one misuse on that object, readable at any time.
- A validity query (`P..._IsValid`) on a stale id is not misuse and is
  not counted.
- Running out of memory is a returned capacity status. Every
  allocation is checked. Byte sizes derived from counts are computed
  with checked arithmetic (`ckd_mul`, `ckd_add`) and refused when they
  do not fit.
- `PP_ASSERT` is for internal invariants only: states that cannot
  happen unless the library itself is wrong. It is active in debug and
  test builds. Library code never calls `assert` or `abort`.
- Every parser of external bytes (files, network data, platform
  payloads) treats its input as hostile and has a fuzz target.

Checked by: compiler warnings (`PP_NODISCARD`); `tools/check_source.py`
(`assert`, `abort`, thread-local variables); `tools/check_docs.py`
(documented statuses); review (fuzz targets).

## 9. Memory

- No library has a process-wide allocator hook. Every owner object
  takes an allocator in its def and keeps it for its lifetime; a zeroed
  allocator means the C library's allocation functions.
- The allocator is two functions and a context:
  `alloc(size, alignment, context)` and
  `free(memory, size, alignment, context)`. Alignment is a power of
  two. There is no realloc.
- A function without an owner object does not allocate. It works in
  caller memory: a buffer and its capacity, and when the buffer is too
  small it reports the size it needs.
- Hot paths never allocate.
- All library memory goes through the allocator. Library code never
  calls `malloc`, `calloc`, `realloc` or `free`.

Checked by: `tools/check_source.py` (direct allocation calls);
tests with a counting allocator (hot paths); review.

## 10. Threads

- A library starts no thread unless a platform leaves no other way to
  meet a requirement. Such a thread is declared in the library profile
  with its purpose and bound, started by the root or an object it owns
  only while needed, joined by that object's stop or destroy and never
  later than the root's destroy, and never runs application code
  (record 0017).
- On a thread the platform owns (an audio callback, a platform
  callback), a library only publishes into bounded queues or reads what
  was already published: no allocation, lock or wait, and no
  application code except a callback the profile declares real-time.
- Parallel work runs on the host's task system through the library's
  task hooks.
- The `@par Thread safety` paragraph of every public function opens
  with one of these statements; further sentences may follow:
  - `Safe from any thread.`
  - `Safe from any thread; <an object> is used by one thread at a time.`
  - `Main thread only.` (the thread the platform requires)
  - `Safe from any thread; it runs on the main thread, waiting at most
    the marshal deadline.`
  - `Real-time safe: no allocation, lock or wait.`
- Scheduling never changes results.

Checked by: `tools/check_docs.py` (a thread safety paragraph opening
with a listed statement); ThreadSanitizer in CI; review.

## 11. Floating point and determinism

- No fast math. The compiler flags in `cmake/` refuse to configure a
  build that enables it.
- No implicit floating-point contraction. Fused multiply-add appears
  only where code spells it through the family's SIMD header.
- Each library's profile states its determinism class and the rules
  that follow from it.
- A library whose profile promises the same bits on every platform
  lists in `tools/libm-allowed.txt` the `<math.h>` functions its
  sources may call, one per line: only exactly rounded ones (`sqrt`,
  `fabs`, `floor`, `ceil`, `trunc`, `round`, `lround`, `rint`,
  `nearbyint`, `fmin`, `fmax`, `fmod`, `remainder`, `copysign`,
  `frexp`, `ldexp`, `scalbn`, `modf`, `nextafter` and their `f` forms),
  never `fma` (above). Every other `<math.h>` function is refused in
  its sources; the library writes its own where it needs one.

Checked by: `cmake/` (flags); `tools/check_source.py` (calls the
profile bans, and `<math.h>` calls against `tools/libm-allowed.txt`);
the library's determinism tests.

## 12. API design

- Every creation takes a def struct initialized by
  `P..Default..Def`. Defs carry a cookie so that an uninitialized def is
  refused.
- Functions that fill a caller array take the array and its capacity
  and report the true total, which may exceed the capacity. A NULL
  array with capacity 0 counts.
- Getters that cannot fail take ids and return values; they do not
  take out-parameters unless they return more than one value.
- Strings cross the API as UTF-8 with an explicit byte length, never
  as NUL-terminated text alone.
- A library's root object is a typed opaque pointer. Every object a
  root owns is named by a typed value id, `{ uint32_t index1; uint32_t
  generation; }` with 0 as null, one struct per kind; a stale id is
  refused with a typed result (record 0016). Platform handles are never
  identities.
- An operation that finishes later is a request: it returns its status
  at once and, accepted, a typed request id that exactly one completion
  record answers, in the owner's notification queue, with a typed
  outcome (record 0018). No library calls application code to deliver
  a completion.

Checked by: review; tests for every refusal and for request and
completion pairing.

## 13. Tests

- Tests use the family's harness header in `test/` and register with
  CTest through the helper in `CMakeLists.txt`.
- Each test function checks one behavior and is named for it.
- Every fixed bug comes with a test that fails without the fix.
- Tests that need internals include internal headers and are marked
  white-box in `CMakeLists.txt`; all others use the public API only.
- CI runs the tests under AddressSanitizer, UndefinedBehaviorSanitizer
  and ThreadSanitizer, with warnings as errors on GCC and Clang.
- Long-running soaks and fuzzing campaigns stay out of the default
  CTest set and run on a schedule.
- Every C snippet of `docs/guide.md` is built and run by a test, as
  written, in `test/test_guide*.c` (a whole program waiting for its
  user is built only); a fragment no test can build everywhere is
  marked `<!-- guide: not run: REASON -->` right above its fence.

Checked by: CI; `tools/check_guide.py` for the guide's snippets.

## 14. Performance

- Each library has plain C benchmarks in `bench/` with fixed inputs and
  no dependencies. They print their timings.
- A change meant to affect performance, or likely to, is measured
  before and after on the same machine, and the numbers go in its
  commit message.
- CI reports the library's size, in total and per optional feature.
- Hot paths take no locks, make no system calls and call nothing
  locale-dependent from the C library.

Checked by: review; `tools/check_source.py` (locale-dependent calls it
recognizes).

## 15. Versioning, the changelog and releases

- Semantic versioning. The version lives in one place, the
  `PP_VERSION_*` macros in the library's base header; CMake reads it
  from there.
- Before 1.0.0 any minor release may change the API, the ABI and every
  data format. A data format version is bumped whenever its bytes
  change.
- `CHANGELOG.md` follows Keep a Changelog. Every user-visible change
  adds a line under `[Unreleased]` in the same commit, in the section
  order Added, Changed, Deprecated, Removed, Fixed, Security.
- Releases are tags `vX.Y.Z` on `main`, made by following the release
  checklist below in order. A library with steps of its own (a size
  budget, recorded runs on devices) lists them in its
  `docs/releasing.md`, linked from its README, and takes them between
  steps 4 and 5.

The release checklist:

1. `CHANGELOG.md`: `[Unreleased]` becomes `[X.Y.Z] - YYYY-MM-DD`, with
   a line saying what the release is, and an empty `[Unreleased]`
   starts above it.
2. The `PP_VERSION_*` macros are `X.Y.Z`, and the README's status
   names the release.
3. The family drift check (`family_sync.py --check`, run from the
   family's docs repository) reports no difference for the library.
4. These changes are one commit, `release: X.Y.Z`, and CI is green on
   it in every job.
5. An annotated tag `vX.Y.Z` on that commit is pushed, and a release
   on the hosting site carries the changelog section as its notes.
6. A library that pins this one (a seam check, a fetched dependency)
   moves its pin in a commit of its own when it needs the release.

Checked by: review; the release checklist.

## 16. Commits

- Subject: `area: imperative summary`, lowercase area, at most 72
  characters, no trailing period. The areas are listed in the library's
  profile; a commit that spans areas takes the one that matters most.
- Body: wrapped at 72 columns, says what was wrong or missing and why
  this change is right.
- No trailers (`Signed-off-by`, `Co-authored-by`, generated-by lines)
  and no attribution to tools of any kind.
- One logical change per commit. Every commit builds, passes the tests
  and is formatted.

Checked by: review.

## 17. Markdown and outside code

- One `#` title per file, sentence case headings. Prose wrapped at 72
  columns; tables and code blocks are exempt. Code blocks name their
  language. Links between documents are relative.
- No code is copied or adapted from other projects. Algorithms from the
  literature are implemented from their published descriptions, and
  `docs/references.md` lists those sources.
- A library takes no dependencies other than the platform's own APIs
  and system libraries its profile names. An exception is recorded in a
  design record, and the dependency's name and license go in
  `THIRD_PARTY.md` at the repository root.
- Data files from outside (the Unicode Character Database, a gamepad
  mapping database) are inputs, not code. They are kept exactly as
  published, in directories listed in `tools/external-dirs.txt`, each
  with a `README.md` that names the source, the version, the license
  and the SHA-256 of every file. The source checks skip them.
- `testbed/`, `bench/` and test-only harnesses may use outside
  libraries; they are listed in `README.md`.

Checked by: review.

## 18. Library profile

Each library has one design record titled "Library profile" that
states, for that library:

- **Determinism:** its class (bit-exact on every platform, reproducible
  on one platform and build, or exempt) and which outputs it covers.
- **Threads:** any thread the library may start, and why.
- **Memory:** its owner objects, and what is allocated when.
- **Platform dependencies:** the platform APIs and system libraries it
  uses.
- **Commit areas:** the list for section 16.

A rule in a profile may add to this file but never loosen it.
