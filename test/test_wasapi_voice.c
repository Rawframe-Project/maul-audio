// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WASAPI voice processing without Windows 11's effects manager: a fake
// IAudioClient gives a fake IAudioClient2, which records the stream
// properties asked for, and a fake IAudioEffectsManager, whose effects
// the module turns on and off and reports. Wine has neither, so this
// is where the logic runs.

#include "context_core.h"
#include "test_harness.h"
#include "wasapi_voice.h"

#include <combaseapi.h>
#include <string.h>

// The effects manager's layout, as wasapi_voice.c spells it out.
typedef struct Effect
{
    GUID id;
    BOOL canSetState;
    int state;
} Effect;

typedef struct Manager Manager;
typedef struct ManagerVtbl
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(Manager*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(Manager*);
    ULONG(STDMETHODCALLTYPE* Release)(Manager*);
    HRESULT(STDMETHODCALLTYPE* RegisterCallback)(Manager*, void*);
    HRESULT(STDMETHODCALLTYPE* UnregisterCallback)(Manager*, void*);
    HRESULT(STDMETHODCALLTYPE* GetAudioEffects)(Manager*, Effect**, UINT32*);
    HRESULT(STDMETHODCALLTYPE* SetAudioEffectState)(Manager*, GUID, int);
} ManagerVtbl;

struct Manager
{
    const ManagerVtbl* lpVtbl;
    Effect effects[5];
    UINT32 count;
    bool fails;
    int sets;
};

#define EFFECT(first) {(first), 0x8211, 0x11E2, {0x8C, 0x70, 0x2C, 0x27, 0xD7, 0xF0, 0x01, 0xFA}}
static const GUID s_echo = EFFECT(0x6F64ADBE);
static const GUID s_noise = EFFECT(0x6F64ADBF);
static const GUID s_deepNoise = EFFECT(0x6F64ADD0);
static const GUID s_gain = EFFECT(0x6F64ADC0);
static const GUID s_other = EFFECT(0x12345678);

static ULONG STDMETHODCALLTYPE ManagerRelease(Manager* manager)
{
    (void)manager;
    return 0;
}

static HRESULT STDMETHODCALLTYPE GetEffects(Manager* manager, Effect** effects, UINT32* count)
{
    if (manager->fails)
    {
        return E_FAIL;
    }
    *effects = CoTaskMemAlloc(sizeof(manager->effects));
    memcpy(*effects, manager->effects, sizeof(manager->effects));
    *count = manager->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE SetEffect(Manager* manager, GUID id, int state)
{
    for (UINT32 i = 0; i < manager->count; ++i)
    {
        if (memcmp(&manager->effects[i].id, &id, sizeof(id)) == 0)
        {
            manager->effects[i].state = state;
        }
    }
    manager->sets++;
    return S_OK;
}

static const ManagerVtbl s_managerVtbl = {
    .Release = ManagerRelease, .GetAudioEffects = GetEffects, .SetAudioEffectState = SetEffect};

// The fake client: IAudioClient2 through QueryInterface, the manager
// through GetService, and what SetClientProperties was given.
typedef struct Client
{
    IAudioClient client;
    IAudioClient2 client2;
    Manager* manager;
    AudioClientProperties properties;
    int propertiesSet;
    bool hasClient2;
} Client;

static Client* s_client;

static HRESULT STDMETHODCALLTYPE QueryInterface(IAudioClient* client, REFIID iid, void** out)
{
    (void)client;
    (void)iid;
    *out = s_client->hasClient2 ? &s_client->client2 : nullptr;
    return s_client->hasClient2 ? S_OK : E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE GetService(IAudioClient* client, REFIID iid, void** out)
{
    (void)client;
    (void)iid;
    *out = s_client->manager;
    return s_client->manager != nullptr ? S_OK : E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Client2Release(IAudioClient2* client)
{
    (void)client;
    return 0;
}

static HRESULT STDMETHODCALLTYPE SetProperties(IAudioClient2* client,
                                               const AudioClientProperties* properties)
{
    (void)client;
    s_client->properties = *properties;
    s_client->propertiesSet++;
    return S_OK;
}

static IAudioClientVtbl s_clientVtbl;
static IAudioClient2Vtbl s_client2Vtbl;

static void MakeClient(Client* client, Manager* manager)
{
    s_clientVtbl.QueryInterface = QueryInterface;
    s_clientVtbl.GetService = GetService;
    s_client2Vtbl.Release = Client2Release;
    s_client2Vtbl.SetClientProperties = SetProperties;
    *client = (Client){.client = {.lpVtbl = &s_clientVtbl},
                       .client2 = {.lpVtbl = &s_client2Vtbl},
                       .manager = manager,
                       .hasClient2 = true};
    s_client = client;
}

static maudStreamCore Core(maudDirection direction, maudVoiceProcessing voice)
{
    maudStreamCore core = {0};
    core.def.direction = direction;
    core.def.voice = voice;
    return core;
}

// The category and options a stream asks for before it initializes.
static void TestAsk(void)
{
    Client client;
    MakeClient(&client, nullptr);
    maudStreamCore voiced = Core(maud_directionInput, maud_voiceEchoCancellation);
    maudWasapiAskForVoice(&client.client, &voiced);
    CHECK(client.propertiesSet == 1 &&
              client.properties.eCategory == AudioCategory_Communications &&
              client.properties.Options == AUDCLNT_STREAMOPTIONS_NONE,
          "a voiced input is a communications stream");
    maudStreamCore raw = Core(maud_directionInput, maud_voiceNone);
    maudWasapiAskForVoice(&client.client, &raw);
    CHECK(client.propertiesSet == 2 && client.properties.eCategory == AudioCategory_Other &&
              client.properties.Options == AUDCLNT_STREAMOPTIONS_RAW,
          "an input that asks for none asks for the raw signal");
    maudStreamCore output = Core(maud_directionOutput, maud_voiceNone);
    maudWasapiAskForVoice(&client.client, &output);
    CHECK(client.propertiesSet == 2, "a plain output asks for nothing");
    maudStreamCore voicedOutput = Core(maud_directionOutput, maud_voiceEchoCancellation);
    maudWasapiAskForVoice(&client.client, &voicedOutput);
    CHECK(client.propertiesSet == 3 && client.properties.eCategory == AudioCategory_Communications,
          "a voiced duplex output is a communications stream too");
    client.hasClient2 = false;
    maudWasapiAskForVoice(&client.client, &voiced);
    CHECK(client.propertiesSet == 3, "a client without IAudioClient2 keeps its defaults");
}

// The asked-for parts turned on and the others off, where the stream
// may, and the parts on reported.
static void TestReport(void)
{
    // Four effects listed; the fifth, past the count, must not be read.
    Manager manager = {.lpVtbl = &s_managerVtbl, .count = 4};
    manager.effects[0] = (Effect){s_echo, TRUE, 0};
    manager.effects[1] = (Effect){s_noise, TRUE, 1};
    manager.effects[2] = (Effect){s_gain, FALSE, 1};
    manager.effects[3] = (Effect){s_other, TRUE, 1};
    manager.effects[4] = (Effect){s_deepNoise, FALSE, 1};
    Client client;
    MakeClient(&client, &manager);
    maudStreamCore core = Core(maud_directionInput, maud_voiceEchoCancellation);
    maudWasapiReportVoice(&client.client, &core);
    CHECK(manager.effects[0].state == 1 && manager.effects[1].state == 0,
          "the asked-for part turned on, an unasked one off");
    CHECK(manager.effects[2].state == 1 && manager.effects[3].state == 1 && manager.sets == 2,
          "fixed effects and others left alone");
    CHECK(atomic_load(&core.voiceReported) &&
              atomic_load(&core.voiceActive) ==
                  (maud_voiceEchoCancellation | maud_voiceGainControl),
          "the parts on reported, a fixed one among them");
    // Nothing on before setting: the report is still made, of what is on
    // after; and a report of none is a report.
    Manager quiet = {.lpVtbl = &s_managerVtbl, .count = 2};
    quiet.effects[0] = (Effect){s_echo, TRUE, 0};
    quiet.effects[1] = (Effect){s_noise, TRUE, 0};
    client.manager = &quiet;
    maudStreamCore asked = Core(maud_directionInput, maud_voiceNoiseSuppression);
    maudWasapiReportVoice(&client.client, &asked);
    CHECK(atomic_load(&asked.voiceReported) &&
              atomic_load(&asked.voiceActive) == maud_voiceNoiseSuppression,
          "a part turned on from none on");
    quiet.effects[1].state = 1;
    maudStreamCore none = Core(maud_directionInput, maud_voiceNone);
    maudWasapiReportVoice(&client.client, &none);
    CHECK(atomic_load(&none.voiceReported) && atomic_load(&none.voiceActive) == maud_voiceNone,
          "none on is reported as none");
    client.manager = &manager;
    maudStreamCore output = Core(maud_directionOutput, maud_voiceEchoCancellation);
    maudWasapiReportVoice(&client.client, &output);
    CHECK(!atomic_load(&output.voiceReported), "an output reports nothing");
    manager.fails = true;
    maudStreamCore unread = Core(maud_directionInput, maud_voiceNone);
    maudWasapiReportVoice(&client.client, &unread);
    CHECK(!atomic_load(&unread.voiceReported), "a list that cannot be read reports nothing");
    client.manager = nullptr;
    maudWasapiReportVoice(&client.client, &unread);
    CHECK(!atomic_load(&unread.voiceReported), "nor does a stream without the manager");
}

int main(void)
{
    TestAsk();
    TestReport();
    return s_failures == 0 ? 0 : 1;
}
