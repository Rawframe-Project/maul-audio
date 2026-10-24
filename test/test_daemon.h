// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drives the sound-server fixture's daemon (tools/pipewire_daemon.sh), for
// the backend tests that stop, start or signal it, and reads the xruns its
// graph counted, which the timing checks judge their windows by. CMake
// compiles the script and the fixture's directory in where the fixture
// runs, so no command comes from the environment; elsewhere there is no
// daemon.

#ifndef MAUL_AUDIO_TEST_TEST_DAEMON_H
#define MAUL_AUDIO_TEST_TEST_DAEMON_H

#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

// Starts reading the xruns every node of the fixture's server has
// counted, summed (the script's xruns action), which takes about a
// second; NULL where there is no daemon. GraphXrunsEnd takes the count.
static inline FILE* GraphXrunsBegin(void)
{
    char command[1024];
    int length =
        snprintf(command, sizeof(command), "%s xruns %s", MAUD_TEST_DAEMON, MAUD_TEST_SERVER_DIR);
    return HaveDaemon() && length > 0 && length < (int)sizeof(command) ? popen(command, "r")
                                                                       : nullptr;
}

static inline bool GraphXrunsEnd(FILE* reading, unsigned long long* count)
{
    if (reading == nullptr)
    {
        return false;
    }
    bool read = fscanf(reading, "%llu", count) == 1;
    return pclose(reading) == 0 && read;
}

// A window of a timing check against the fixture's server: `milliseconds`
// (at least the second the count takes) through which the context, when
// set, is drained (streams move, and PipeWire's main loop runs, inside the
// library's calls on this thread). It is clean when no node of the graph
// counted an xrun since the window before, the stream counted none, and
// none of this thread's 10 ms sleeps woke more than LATE_WAKEUP late: a
// process that cannot get the CPU on time starves its own audio threads
// too, whose gaps a server may fill with silence and never count.
typedef struct GraphWatch
{
    maudContext* context;
    maudStreamId stream;
    int milliseconds;
    unsigned long long xruns;
    uint64_t streamXruns;
    bool counted;
} GraphWatch;

// How late a 10 ms sleep may wake before the window counts as starved.
#define LATE_WAKEUP 10000000LL

// The stream's own xruns so far; 0 without a stream.
static inline uint64_t StreamXruns(const GraphWatch* watch)
{
    maudStreamStatus status = {0};
    if (watch->stream.index1 == 0)
    {
        return 0;
    }
    return maudGetStreamStatus(watch->context, watch->stream, &status) == maud_success
               ? status.underruns + status.overruns
               : UINT64_MAX;
}

static inline bool GraphWindow(void* user)
{
    GraphWatch* watch = user;
    FILE* reading = GraphXrunsBegin();
    int64_t latest = 0;
    for (int i = 0; i < watch->milliseconds / 10; ++i)
    {
        maudNotification ignored;
        while (watch->context != nullptr &&
               maudNextNotification(watch->context, &ignored) == maud_success)
        {
        }
        struct timespec pause = {0, 10000000L};
        int64_t before = maudGetHostNanoseconds();
        nanosleep(&pause, nullptr);
        int64_t late = maudGetHostNanoseconds() - before - 10000000LL;
        latest = late > latest ? late : latest;
    }
    unsigned long long xruns = 0;
    bool read = GraphXrunsEnd(reading, &xruns);
    uint64_t streamXruns = StreamXruns(watch);
    bool clean = read && watch->counted && xruns == watch->xruns &&
                 streamXruns == watch->streamXruns && latest <= LATE_WAKEUP;
    watch->xruns = xruns;
    watch->streamXruns = streamXruns;
    watch->counted = read;
    return clean;
}

#endif // MAUL_AUDIO_TEST_TEST_DAEMON_H
