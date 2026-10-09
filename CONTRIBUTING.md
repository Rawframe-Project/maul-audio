# Contributing

This library is part of the Maul family, whose libraries share one set
of rules. Read [docs/conventions.md](docs/conventions.md) before writing
code; it is the same file in every Maul repository.

## Before you open a pull request

- The build is warning-free and every test passes in Debug and
  Release:

  ```sh
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ctest --test-dir build
  ```

- The code is formatted with the pinned `clang-format`, and the checks
  in `tools/` pass.
- A bug fix comes with a test that fails without it.
- A user-visible change adds a line to `CHANGELOG.md` under
  `[Unreleased]`.
- A change meant to affect performance carries before-and-after
  benchmark numbers from the same machine in its commit message.
- A change that moves a pinned value (a determinism hash, a generated
  table) says which and why in the commit message and the changelog.

## Commits and pull requests

Commit subjects read `area: imperative summary`; the body says what
was wrong and why the change is right. The full rules are in the
conventions. A pull request carries one topic, and its title follows
the same form as a commit subject.

## Releases

A release follows the checklist in the conventions (section 15) and,
where the library has one, its own steps in `docs/releasing.md`.

## Reporting bugs

A report with a small program that reproduces the problem is the most
useful kind. Include the platform, the compiler and the library
version.

## License

Contributions are accepted under the MIT license that covers the
project.
