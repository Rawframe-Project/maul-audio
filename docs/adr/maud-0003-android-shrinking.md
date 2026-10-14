# maud-0003. What an Android application keeps when it shrinks its code

Status: Accepted

## Context

On Android the application compiles the library's Java
(`java/maul/audio`) with its own. The native library loads
`maul.audio.Devices` by name through the application's class loader,
registers its native methods on it and calls its constructor and six
methods by name and signature. A release build shrinks, optimizes and
renames its code with R8 (Gradle's default) or ProGuard, and keeps only
what its rules or its own code reach; neither sees JNI, so such an
application lost the class, and creating a context with its handles
failed with `maud_errorPlatform`. Nothing said what to keep, and the
test application, built with d8 alone, could not show it.

## Decision

- The library ships `java/proguard-rules.pro`, keeping every class of
  the `maul.audio` package whole. An application that shrinks its code
  adds the file to its rules; the guide and the context def's comment
  say so.
- The test application is built by R8 (shrinking, optimizing and
  renaming, as a release build) with that file and the manifest's rules
  from aapt2, so every Android test run checks the library's rules.
  Without the file R8 keeps none of the library's classes and the test
  application cannot be built.

## Consequences

Shrunk applications keep working, and a Java member the native library
reaches by name without a rule keeping it fails the Android test. The
rules keep the package's few classes whole rather than member by
member, which costs an application a few kilobytes and cannot miss one.
This follows Maul Window's record for the same trap.
