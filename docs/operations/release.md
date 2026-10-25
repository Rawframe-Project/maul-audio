# Release

Releases are made by release-please from the conventional commit titles on `main`. Versions follow Semantic Versioning; before 1.0.0 a breaking change bumps the minor version, a feature the minor and a fix the patch. Tags are `vX.Y.Z`.

## Steps

1. release-please keeps a draft pull request titled `chore: release X.Y.Z` open. It moves the version between the markers in `include/maul-audio/base.h`, where CMake reads it and `MAUD_ABI_VERSION` follows, updates `.release-please-manifest.json`, and adds an entry to `CHANGELOG.md` from the `feat`, `fix` and `perf` titles and every `BREAKING CHANGE` footer.
2. Rewrite that entry in user language before merging: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security. Leave out refactors, tests, CI and documentation. Say what a caller must change for a breaking change. Name any change of results, such as a golden render, a bake's bytes or a stream's timing, and who hears it, since programs ship bakes and tune by ear.
3. Check that the `required` CI job passed on the release pull request, and that the nightly extended run on `main` passed since the last change. To run it now rather than wait for the night, dispatch it: `gh workflow run ci.yml --ref main -f nightly=true`, which runs it with the schedule's settings.
4. Mark the pull request ready and merge it. release-please tags the merge commit and publishes the GitHub release with the changelog entry as its notes. The Pages workflow publishes the documentation from `main`.
5. The `assets` job of `.github/workflows/release.yml` then attaches the source archive `maul-audio-X.Y.Z.tar.gz` (the tagged tree, with the HRTF set, the golden data and the Java half), its `SHA256SUMS`, an SPDX SBOM and a build provenance attestation. Check that they are on the release; a user verifies a download with `sha256sum -c SHA256SUMS` and `gh attestation verify maul-audio-X.Y.Z.tar.gz -R Rawframe-Project/maul-audio`.

## Notes

- Before 1.0 every release may break the binary interface, so `MAUD_ABI_VERSION` includes the minor and the patch: a program refuses to start against any other release's library (`maud_errorVersion`), the shared library's SOVERSION is the whole version, and the CMake package takes only its exact version.
- A change to a file format changes the format's version in `docs/internals/file-formats.md`; a reader that stops taking an old version is a breaking change.
- The shared family files must match their canonical copies in the organization's `.github` repository before a release.
- The device checklist ([device-checklist.md](device-checklist.md)) is run on real hardware before a release that changes a backend.
- A library or engine that pins this one moves its pin in a pull request of its own.
