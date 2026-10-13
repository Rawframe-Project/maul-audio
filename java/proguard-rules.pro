# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# What an application that shrinks or obfuscates its code (R8, ProGuard)
# keeps for Maul Audio. The native library loads the classes of
# maul.audio by name, calls their constructors and methods by name and
# signature through JNI and registers its native methods on them, none
# of which a shrinker can see, so none may be renamed or removed.
-keep class maul.audio.* { *; }
