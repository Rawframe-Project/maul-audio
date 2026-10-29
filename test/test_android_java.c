// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's Java half, in an application in the emulator
// (tools/build_android_app.sh, tools/run_android_app.sh): a context
// given the activity's Java VM and the activity lists the speaker and
// the microphone beside the defaults, with their forms and the keys
// Android's own listing gives them, and leaves the emulator's internal
// endpoints out; a stream pinned to the speaker plays; an input stream
// waits for the microphone permission, which the library asks for,
// leaves the wait when the runner grants it, and captures; one handle
// without the other is refused; audio focus is held when asked for,
// follows what another request (the test's own, through AudioManager)
// does to it, and is released. The test runs on a thread of its own,
// which the library attaches to the VM only while it calls Java.

#include "test_harness.h"
#include "test_wait.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <android/native_activity.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/system_properties.h>
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
static atomic_uint s_captured;

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudContext_NextNotification(context, &ignored) == maud_success)
    {
    }
}

// Drains notifications; whether one among them says stream resumed.
static bool ResumedAmong(maudContext* context, maudStreamId stream)
{
    bool resumed = false;
    maudNotification record;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        resumed = resumed || (record.kind == maud_notifyStreamResumed &&
                              record.stream.index1 == stream.index1);
    }
    return resumed;
}

// Drains notifications; whether a focus record among them says focus.
static bool FocusAmong(maudContext* context, maudFocus focus)
{
    bool seen = false;
    maudNotification record;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        seen = seen || (record.kind == maud_notifyFocusChanged && record.focus == focus);
    }
    return seen;
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)user;
    if (block->output != nullptr)
    {
        memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
    }
    if (block->input != nullptr && block->output == nullptr)
    {
        atomic_fetch_add(&s_captured, 1);
    }
    atomic_fetch_add(&s_blocks, 1);
}

// The device of a direction whose name is name, or the null id; counts
// the direction's devices into count.
static maudDeviceId Named(const maudContext* context, maudDirection direction, const char* name,
                          uint32_t* count)
{
    maudDeviceId ids[16];
    CHECK(maudContext_GetDevices(context, direction, ids, 16, count) == maud_success, "listed");
    maudDeviceId found = {0, 0};
    for (uint32_t i = 0; i < *count && i < 16; ++i)
    {
        char text[128] = {0};
        char key[128] = {0};
        size_t length = 0;
        maudDeviceInfo info = {0};
        CHECK(
            maudDevice_GetName(context, ids[i], text, sizeof(text) - 1, &length) == maud_success &&
                maudDevice_GetKey(context, ids[i], key, sizeof(key) - 1, &length) == maud_success &&
                maudDevice_GetInfo(context, ids[i], &info) == maud_success,
            "described");
        printf("%s: %s [%s] %u Hz (%u to %u), form %u\n",
               direction == maud_directionOutput ? "output" : "input", text, key,
               info.nativeSampleRate, info.minSampleRate, info.maxSampleRate, (unsigned)info.form);
        // A device Android lists no rates or channel counts for takes the
        // default output's.
        CHECK(info.minSampleRate >= 8000 && info.minSampleRate <= info.nativeSampleRate &&
                  info.nativeSampleRate <= info.maxSampleRate &&
                  maudGetLayoutChannelCount(info.nativeLayout) >= 1,
              "its rates and channels");
        if (strcmp(text, name) == 0)
        {
            found = ids[i];
        }
    }
    return found;
}

// The key Android's own listing gives the first device of type: its
// type and address, or its product name when it has no address. Asks
// AudioManager.getDevices through the test's own JNI.
static bool JavaKey(int type, bool input, char* key, size_t size)
{
    JavaVM* vm = s_run.vm;
    JNIEnv* env = nullptr;
    if ((*vm)->AttachCurrentThread(vm, &env, nullptr) != JNI_OK)
    {
        return false;
    }
    jclass contextType = (*env)->FindClass(env, "android/content/Context");
    jmethodID service = (*env)->GetMethodID(env, contextType, "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject manager =
        (*env)->CallObjectMethod(env, s_run.activity, service, (*env)->NewStringUTF(env, "audio"));
    jclass managerType = (*env)->FindClass(env, "android/media/AudioManager");
    jmethodID list =
        (*env)->GetMethodID(env, managerType, "getDevices", "(I)[Landroid/media/AudioDeviceInfo;");
    // GET_DEVICES_INPUTS 1, GET_DEVICES_OUTPUTS 2.
    jobjectArray devices =
        (jobjectArray)(*env)->CallObjectMethod(env, manager, list, input ? 1 : 2);
    jclass infoType = (*env)->FindClass(env, "android/media/AudioDeviceInfo");
    jmethodID typeOf = (*env)->GetMethodID(env, infoType, "getType", "()I");
    jmethodID addressOf = (*env)->GetMethodID(env, infoType, "getAddress", "()Ljava/lang/String;");
    jmethodID productOf =
        (*env)->GetMethodID(env, infoType, "getProductName", "()Ljava/lang/CharSequence;");
    jmethodID text = (*env)->GetMethodID(env, (*env)->FindClass(env, "java/lang/Object"),
                                         "toString", "()Ljava/lang/String;");
    bool found = false;
    jsize count = devices != nullptr ? (*env)->GetArrayLength(env, devices) : 0;
    for (jsize i = 0; i < count && !found; ++i)
    {
        jobject device = (*env)->GetObjectArrayElement(env, devices, i);
        if ((*env)->CallIntMethod(env, device, typeOf) != type)
        {
            continue;
        }
        jstring address = (jstring)(*env)->CallObjectMethod(env, device, addressOf);
        jobject product = (*env)->CallObjectMethod(env, device, productOf);
        jstring productText =
            product != nullptr ? (jstring)(*env)->CallObjectMethod(env, product, text) : nullptr;
        const char* where =
            address != nullptr ? (*env)->GetStringUTFChars(env, address, nullptr) : nullptr;
        bool useProduct = where == nullptr || where[0] == '\0';
        if (useProduct && where != nullptr)
        {
            (*env)->ReleaseStringUTFChars(env, address, where);
        }
        if (useProduct)
        {
            address = productText;
            where = productText != nullptr ? (*env)->GetStringUTFChars(env, productText, nullptr)
                                           : nullptr;
        }
        snprintf(key, size, "%d:%s", type, where != nullptr ? where : "");
        if (where != nullptr)
        {
            (*env)->ReleaseStringUTFChars(env, address, where);
        }
        found = true;
    }
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    (*vm)->DetachCurrentThread(vm);
    return found && !thrown;
}

// A device's key is the one Android's listing gives it.
static bool KeyedAsJava(const maudContext* context, maudDeviceId id, int type, bool input)
{
    char key[128] = {0};
    char wanted[128] = {0};
    size_t length = 0;
    bool ok = maudDevice_GetKey(context, id, key, sizeof(key) - 1, &length) == maud_success &&
              JavaKey(type, input, wanted, sizeof(wanted));
    printf("key %s, Android's %s\n", key, wanted);
    return ok && strlen(wanted) > 2 && strcmp(key, wanted) == 0;
}

// Whether the test runs in the emulator.
static bool Emulated(void)
{
    char value[PROP_VALUE_MAX] = {0};
    return __system_property_get("ro.kernel.qemu", value) > 0 && value[0] == '1';
}

static void TestDevices(maudContext* context, maudDeviceId* speaker)
{
    uint32_t outputs = 0;
    uint32_t inputs = 0;
    *speaker = Named(context, maud_directionOutput, "Speaker", &outputs);
    maudDeviceId microphone = Named(context, maud_directionInput, "Microphone", &inputs);
    CHECK(speaker->index1 != 0 && microphone.index1 != 0, "the speaker and the microphone");
    CHECK(outputs == 2 && inputs == 2, "beside the defaults, and nothing internal");
    // AudioDeviceInfo TYPE_BUILTIN_SPEAKER 2 (the emulator's has no
    // address, so its product names it), TYPE_BUILTIN_MIC 15.
    CHECK(KeyedAsJava(context, *speaker, 2, false) && KeyedAsJava(context, microphone, 15, true),
          "keyed by their addresses, or their product names");
    maudDeviceInfo info = {0};
    CHECK(maudDevice_GetInfo(context, *speaker, &info) == maud_success &&
              info.form == maud_formSpeakers,
          "speakers");
    CHECK(maudDevice_GetInfo(context, microphone, &info) == maud_success &&
              info.form == maud_formMicrophone,
          "a microphone");
    // The default output follows the route Android's Spatializer works
    // on, so Java tells its state; a pinned output's is the system's.
    maudDeviceId current = {0, 0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &current) ==
                  maud_success &&
              maudDevice_GetInfo(context, current, &info) == maud_success,
          "the default output");
    printf("spatializer %u, head tracking %d\n", (unsigned)info.spatializer, info.headTracking);
    CHECK(info.spatializer != maud_spatializerUnknown && info.spatialObjectCount == 0,
          "with the Spatializer's state, and no objects");
    // The emulator's route has none at any API level, so its state is
    // exactly none there.
    CHECK(!Emulated() || info.spatializer == maud_spatializerNone, "none in the emulator");
    CHECK(maudDevice_GetInfo(context, *speaker, &info) == maud_success &&
              info.spatializer == maud_spatializerUnknown,
          "a pinned output's left unknown");
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &current) ==
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
              maudStream_Start(context, stream) == maud_success,
          "a stream on the speaker");
    atomic_store(&s_blocks, 0);
    POLL_UNTIL(3000, "the speaker's blocks", Drain(context), atomic_load(&s_blocks) >= 20);
    CHECK(atomic_load(&s_blocks) >= 20, "it plays");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
}

static maudSuspendReason Suspension(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success, "status");
    return status.suspension;
}

static void TestPermission(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_Start(context, stream) == maud_success,
          "an input stream");
    CHECK(Suspension(context, stream) == maud_suspendPermission, "waits for the microphone");
    STEP_FOR(300, Drain(context));
    CHECK(Suspension(context, stream) == maud_suspendPermission, "and goes on waiting");
    printf("adb: pm grant %s android.permission.RECORD_AUDIO\n", MAUD_TEST_PACKAGE);
    bool resumed = false;
    POLL_UNTIL(15000, "the input stream's resumption once granted",
               resumed = ResumedAmong(context, stream) || resumed, resumed);
    CHECK(resumed && Suspension(context, stream) != maud_suspendPermission,
          "granted, it leaves the wait");
    atomic_store(&s_captured, 0);
    POLL_UNTIL(3000, "the captured blocks", Drain(context), atomic_load(&s_captured) >= 20);
    CHECK(atomic_load(&s_captured) >= 20, "and captures");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
}

// Another client's focus request, through the test's own JNI: the
// AudioManager's request of AudioFocusRequest gain, or its abandonment
// (gain 0) of the last one.
static bool OtherFocus(int gain)
{
    static jobject s_request;
    JavaVM* vm = s_run.vm;
    JNIEnv* env = nullptr;
    if ((*vm)->AttachCurrentThread(vm, &env, nullptr) != JNI_OK)
    {
        return false;
    }
    jobject activity = s_run.activity;
    jclass contextType = (*env)->FindClass(env, "android/content/Context");
    jmethodID service = (*env)->GetMethodID(env, contextType, "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject manager =
        (*env)->CallObjectMethod(env, activity, service, (*env)->NewStringUTF(env, "audio"));
    jclass managerType = (*env)->FindClass(env, "android/media/AudioManager");
    jint result = 0;
    if (gain == 0 && s_request != nullptr)
    {
        jmethodID abandon = (*env)->GetMethodID(env, managerType, "abandonAudioFocusRequest",
                                                "(Landroid/media/AudioFocusRequest;)I");
        result = (*env)->CallIntMethod(env, manager, abandon, s_request);
        (*env)->DeleteGlobalRef(env, s_request);
        s_request = nullptr;
    }
    else if (gain != 0)
    {
        jclass builderType = (*env)->FindClass(env, "android/media/AudioFocusRequest$Builder");
        jobject builder = (*env)->NewObject(
            env, builderType, (*env)->GetMethodID(env, builderType, "<init>", "(I)V"), gain);
        jobject request = (*env)->CallObjectMethod(
            env, builder,
            (*env)->GetMethodID(env, builderType, "build", "()Landroid/media/AudioFocusRequest;"));
        jmethodID ask = (*env)->GetMethodID(env, managerType, "requestAudioFocus",
                                            "(Landroid/media/AudioFocusRequest;)I");
        result = (*env)->CallIntMethod(env, manager, ask, request);
        s_request = (*env)->NewGlobalRef(env, request);
    }
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    (*vm)->DetachCurrentThread(vm);
    return !thrown && result == 1;
}

// Drains until a focus record says focus, two seconds at most.
static bool FocusBecomes(maudContext* context, maudFocus focus)
{
    bool seen = false;
    POLL_UNTIL(2000, "the focus record", seen = FocusAmong(context, focus) || seen, seen);
    maudFocus now = maud_focusNone;
    CHECK(maudContext_GetFocus(context, &now) == maud_success, "focus read");
    if (!seen || now != focus)
    {
        printf("focus: wanted %u, now %u\n", (unsigned)focus, (unsigned)now);
    }
    return seen && now == focus;
}

// AudioManager's gains, for the other client.
#define GAIN                    1
#define GAIN_TRANSIENT          2
#define GAIN_TRANSIENT_MAY_DUCK 3

// Focus while a stream plays, as Android ducks a playing application
// itself unless its request says it pauses when ducked.
static void TestFocus(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_Start(context, stream) == maud_success,
          "a stream plays");
    CHECK(maudContext_RequestFocus(context, maud_focusLasting, maud_roleGeneral) == maud_success,
          "focus asked for");
    CHECK(FocusBecomes(context, maud_focusHeld), "and held");
    CHECK(OtherFocus(GAIN_TRANSIENT) && FocusBecomes(context, maud_focusPaused),
          "paused while another takes it for a while");
    CHECK(OtherFocus(0) && FocusBecomes(context, maud_focusHeld), "held again after");
    CHECK(OtherFocus(GAIN_TRANSIENT_MAY_DUCK) && FocusBecomes(context, maud_focusDucked),
          "ducked, reported rather than applied");
    CHECK(OtherFocus(0) && FocusBecomes(context, maud_focusHeld), "held again after the duck");
    CHECK(OtherFocus(GAIN) && FocusBecomes(context, maud_focusLost),
          "lost when another takes it for good");
    CHECK(OtherFocus(0), "the other gives it back");
    CHECK(maudContext_RequestFocus(context, maud_focusBrief, maud_roleCommunications) ==
                  maud_success &&
              FocusBecomes(context, maud_focusHeld),
          "asked for again, for a call");
    CHECK(maudContext_RequestFocus(context, maud_focusRelease, maud_roleGeneral) == maud_success &&
              FocusBecomes(context, maud_focusNone),
          "released");
    CHECK(maudContext_RequestFocus(context, maud_focusRelease, maud_roleGeneral) == maud_success,
          "released again");
    maudNotification record;
    bool again = false;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        again = again || record.kind == maud_notifyFocusChanged;
    }
    CHECK(!again, "an unchanged state is not posted again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "the stream destroyed");
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
    printf("context: %s\n", maudGetResultName(result));
    CHECK(result == maud_success, "a context with the Java half");
    if (context != nullptr)
    {
        maudDeviceId speaker = {0, 0};
        TestDevices(context, &speaker);
        TestPinned(context, speaker);
        TestPermission(context);
        TestFocus(context);
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
