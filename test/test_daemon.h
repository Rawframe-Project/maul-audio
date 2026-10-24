// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drives the sound-server fixture's daemon (tools/pipewire_daemon.sh), for
// the backend tests that stop, start or signal it. CMake compiles the
// script and the fixture's directory in where the fixture runs, so no
// command comes from the environment; elsewhere there is no daemon.

#ifndef MAUL_AUDIO_TEST_TEST_DAEMON_H
#define MAUL_AUDIO_TEST_TEST_DAEMON_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef MAUD_TEST_DAEMON
#define MAUD_TEST_DAEMON     ""
#define MAUD_TEST_SERVER_DIR ""
#endif

// Whether this build has the fixture's daemon to drive.
static inline bool HaveDaemon(void)
{
    return MAUD_TEST_DAEMON[0] != '\0';
}

// Runs the script's action ("stop", "start", "signal") on the fixture's
// directory, with more arguments after it (for "signal", the process and
// the signal), and says whether it succeeded. The script signals only
// processes it started.
static inline bool DriveDaemon(const char* action, const char* more)
{
    char command[1024];
    int length = snprintf(command, sizeof(command), "%s %s %s %s", MAUD_TEST_DAEMON, action,
                          MAUD_TEST_SERVER_DIR, more);
    return HaveDaemon() && length > 0 && length < (int)sizeof(command) && system(command) == 0;
}

#endif // MAUL_AUDIO_TEST_TEST_DAEMON_H
