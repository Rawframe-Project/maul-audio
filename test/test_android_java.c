// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's Java half, in an application in the emulator
// (tools/build_android_app.sh, tools/run_android_app.sh): a context
// given the activity's Java VM and the activity lists the speaker and
// the microphone beside the defaults, with their forms, and leaves the
// emulator's internal endpoints out; a stream pinned to the speaker
// plays; an input stream waits for the microphone permission, which the
// library asks for, and leaves the wait when the runner grants it; one
// handle without the other is refused. The test runs on a thread of its
// own, which the library attaches to the VM only while it calls Java.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <android/native_activity.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct Run
{
    void* vm;
    void* activity;
    char out[512];
} Run;

static Run s_run;
static atomic_uint s_blocks;

static void Sleep(int milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, nullptr);
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)user;
    if (block->output != nullptr)
    {
        memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
    }
    atomic_fetch_add(&s_blocks, 1);
}

// The device of a direction whose name is name, or the null id; counts
// the direction's devices into count.
static maudDeviceId Named(const maudContext* context, maudDirection direction, const char* name,
                          uint32_t* count)
{
    maudDeviceId ids[16];
    CHECK(maudGetDevices(context, direction, ids, 16, count) == maud_success, "listed");
    maudDeviceId found = {0, 0};
    for (uint32_t i = 0; i < *count && i < 16; ++i)
    {
        char text[128] = {0};
        char key[128] = {0};
        size_t length = 0;
        maudDeviceInfo info = {0};
        CHECK(maudGetDeviceName(context, ids[i], text, sizeof(text) - 1, &length) == maud_success &&
                  maudGetDeviceKey(context, ids[i], key, sizeof(key) - 1, &length) ==
                      maud_success &&
                  maudGetDeviceInfo(context, ids[i], &info) == maud_success,
              "described");
        printf("%s: %s [%s] %u Hz (%u to %u), form %u\n",
               direction == maud_directionOutput ? "output" : "input", text, key,
               info.nativeSampleRate, info.minSampleRate, info.maxSampleRate, (unsigned)info.form);
        if (strcmp(text, name) == 0)
        {
            found = ids[i];
        }
    }
    return found;
}

static void TestDevices(maudContext* context, maudDeviceId* speaker)
{
    uint32_t outputs = 0;
    uint32_t inputs = 0;
    *speaker = Named(context, maud_directionOutput, "Speaker", &outputs);
    maudDeviceId microphone = Named(context, maud_directionInput, "Microphone", &inputs);
    CHECK(speaker->index1 != 0 && microphone.index1 != 0, "the speaker and the microphone");
    CHECK(outputs == 2 && inputs == 2, "beside the defaults, and nothing internal");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, *speaker, &info) == maud_success &&
              info.form == maud_formSpeakers,
          "speakers");
    CHECK(maudGetDeviceInfo(context, microphone, &info) == maud_success &&
              info.form == maud_formMicrophone,
          "a microphone");
    maudDeviceId current = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &current) ==
                  maud_success &&
              current.index1 != speaker->index1,
          "the default stays Android's to route");
}

static void TestPinned(maudContext* context, maudDeviceId speaker)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.device = speaker;
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success,
          "a stream on the speaker");
    atomic_store(&s_blocks, 0);
    for (int tries = 0; tries < 300 && atomic_load(&s_blocks) < 20; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    CHECK(atomic_load(&s_blocks) >= 20, "it plays");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
}

static maudSuspendReason Suspension(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    return status.suspension;
}

static void TestPermission(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success,
          "an input stream");
    CHECK(Suspension(context, stream) == maud_suspendPermission, "waits for the microphone");
    printf("adb: pm grant %s android.permission.RECORD_AUDIO\n", MAUD_TEST_PACKAGE);
    bool resumed = false;
    for (int tries = 0; tries < 1500 && !resumed; ++tries)
    {
        maudNotification record;
        while (maudNextNotification(context, &record) == maud_success)
        {
            resumed = resumed || (record.kind == maud_notifyStreamResumed &&
                                  record.streamId.index1 == stream.index1);
        }
        Sleep(10);
    }
    CHECK(resumed && Suspension(context, stream) != maud_suspendPermission,
          "granted, it leaves the wait");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
}

static void TestHalfHandles(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.androidJavaVm = s_run.vm;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_errorInvalid, "a VM without a Context");
    def = maudDefaultContextDef();
    def.androidContext = s_run.activity;
    CHECK(maudCreateContext(&def, &context) == maud_errorInvalid, "a Context without a VM");
}

static void* Main(void* unused)
{
    (void)unused;
    if (freopen(s_run.out, "w", stdout) == nullptr)
    {
        return nullptr;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    maudContextDef def = maudDefaultContextDef();
    def.androidJavaVm = s_run.vm;
    def.androidContext = s_run.activity;
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    printf("context: %s\n", maudResultName(result));
    CHECK(result == maud_success, "a context with the Java half");
    if (context != nullptr)
    {
        maudDeviceId speaker = {0, 0};
        TestDevices(context, &speaker);
        TestPinned(context, speaker);
        TestPermission(context);
        CHECK(maudDestroyContext(context) == maud_success, "destroyed");
    }
    TestHalfHandles();
    printf("result: %d failures\n", s_failures);
    return nullptr;
}

// The activity's creation: the test starts on its own thread, and the
// activity's main thread goes back to its looper, which delivers the
// device callbacks and shows the permission dialog.
JNIEXPORT void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState,
                                        size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;
    s_run.vm = activity->vm;
    s_run.activity = activity->clazz;
    snprintf(s_run.out, sizeof(s_run.out), "%s/out", activity->internalDataPath);
    pthread_t thread;
    if (pthread_create(&thread, nullptr, Main, nullptr) == 0)
    {
        pthread_detach(thread);
    }
}
