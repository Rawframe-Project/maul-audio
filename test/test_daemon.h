// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drives the sound-server fixture's daemon (tools/pipewire_daemon.sh), for
// the backend tests that stop, start or signal it, reads the xruns its
// graph counted, which the timing checks judge their windows by, and
// bounds a wait on its session manager. CMake
// compiles the script and the fixture's directory in where the fixture
// runs, so no command comes from the environment; elsewhere there is no
// daemon.

#ifndef MAUL_AUDIO_TEST_TEST_DAEMON_H
#define MAUL_AUDIO_TEST_TEST_DAEMON_H

#include "test_wait.h"

#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#ifndef MAUD_TEST_DAEMON
#define MAUD_TEST_DAEMON     ""
#define MAUD_TEST_SERVER_DIR ""
#endif

// The longest a wait on the fixture's session manager takes, in
// milliseconds: WirePlumber makes a configured default the default the
// server then announces, and finishes what the daemon starts. Thirty
// seconds, since a loaded machine slows it well past its usual tens of
// milliseconds (ten seconds ran out at a load of 200, and five on an
// ASan cell whose bake suite took 147 s). A wait ends as soon as the
// answer comes.
#define SESSION_MANAGER_MS 30000

// How long one shell command a suite runs may take, in seconds. pactl and
// pw-top answer in about a second at most, but wait on a stalled server
// for as long as it stalls.
#define COMMAND_SECONDS 20
// How long the daemon's script may take to stop or start the daemon, in
// seconds: it bounds each wait at 20 (its LIMIT); a stop makes up to four,
// and a start five (the bus, pipewire, pipewire-pulse, the test sink and
// the default) and creates the test nodes. A suite with a bound of its
// own on each test, as test_pulse's TEST_SECONDS, cuts a start or stop
// inside a test at that bound first, naming the test.
#define DAEMON_SECONDS 110

// Writes into line, of size bytes, the shell command that runs command
// under a bound of seconds, its text quoted for sh -c; false when it does
// not fit.
static inline bool BoundedLine(char* line, size_t size, int seconds, const char* command)
{
    int head = snprintf(line, size, "timeout -k 2 %d sh -c '", seconds);
    if (head < 0 || (size_t)head >= size)
    {
        return false;
    }
    size_t at = (size_t)head;
    for (const char* c = command; *c != '\0'; ++c)
    {
        // A quote ends the quoting, is escaped, and starts it again.
        const char* piece = *c == '\'' ? "'\\''" : c;
        size_t length = *c == '\'' ? 4 : 1;
        if (at + length + 2 > size)
        {
            return false;
        }
        memcpy(line + at, piece, length);
        at += length;
    }
    line[at++] = '\'';
    line[at] = '\0';
    return true;
}

// Says so when a command's status shows it was stopped at its bound.
static inline void ReportStopped(int status, const char* command, int seconds)
{
    if (status != -1 && WIFEXITED(status) &&
        (WEXITSTATUS(status) == 124 || WEXITSTATUS(status) == 137))
    {
        printf("the command did not finish within %d s: %s\n", seconds, command);
    }
}

// Runs command through the shell, stopped past seconds; its status, or
// -1 when it could not run.
static inline int RunBounded(const char* command, int seconds)
{
    char line[4096];
    if (!BoundedLine(line, sizeof(line), seconds, command))
    {
        printf("the command is too long to run: %s\n", command);
        return -1;
    }
    int status = system(line);
    ReportStopped(status, command, seconds);
    return status;
}

// Starts command, stopped past seconds, for its output; NULL when it
// could not start. CloseBounded ends it.
static inline FILE* OpenBounded(const char* command, int seconds)
{
    char line[4096];
    return BoundedLine(line, sizeof(line), seconds, command) ? popen(line, "r") : nullptr;
}

static inline int CloseBounded(FILE* reading, const char* command, int seconds)
{
    int status = pclose(reading);
    ReportStopped(status, command, seconds);
    return status;
}

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
    return HaveDaemon() && length > 0 && length < (int)sizeof(command) &&
           RunBounded(command, DAEMON_SECONDS) == 0;
}

// Starts reading the xruns every node of the fixture's server has
// counted, summed (the script's xruns action), which takes about a
// second; NULL where there is no daemon. GraphXrunsEnd takes the count.
static inline FILE* GraphXrunsBegin(void)
{
    char command[1024];
    int length =
        snprintf(command, sizeof(command), "%s xruns %s", MAUD_TEST_DAEMON, MAUD_TEST_SERVER_DIR);
    return HaveDaemon() && length > 0 && length < (int)sizeof(command)
               ? OpenBounded(command, COMMAND_SECONDS)
               : nullptr;
}

static inline bool GraphXrunsEnd(FILE* reading, unsigned long long* count)
{
    if (reading == nullptr)
    {
        return false;
    }
    bool read = fscanf(reading, "%llu", count) == 1;
    return CloseBounded(reading, "the graph's xruns", COMMAND_SECONDS) == 0 && read;
}

// Prints the fixture's graph as pw-top shows it (the script's nodes
// action), every line after `label`: each node's quantum, rate, timings
// and xrun count, for a failed check's log. It takes about a second, and
// prints nothing where there is no daemon.
static inline void PrintGraphNodes(const char* label)
{
    char command[1024];
    int length =
        snprintf(command, sizeof(command), "%s nodes %s", MAUD_TEST_DAEMON, MAUD_TEST_SERVER_DIR);
    FILE* reading = HaveDaemon() && length > 0 && length < (int)sizeof(command)
                        ? OpenBounded(command, COMMAND_SECONDS)
                        : nullptr;
    if (reading == nullptr)
    {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), reading) != nullptr)
    {
        printf("%s: %s", label, line);
    }
    if (CloseBounded(reading, "the graph's nodes", COMMAND_SECONDS) != 0)
    {
        printf("%s: not read\n", label);
    }
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
    return maudStream_GetStatus(watch->context, watch->stream, &status) == maud_success
               ? status.underrunCount + status.overrunCount
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
               maudContext_NextNotification(watch->context, &ignored) == maud_success)
        {
        }
        int64_t before = maudGetHostNanoseconds();
        // hygiene: the 10 ms sleeps whose late wakes judge the window
        Pause(10);
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
