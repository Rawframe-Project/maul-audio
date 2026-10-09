# Device churn: what CI checks and what a person checks

Devices come and go, defaults move, and audio services restart. The
tests simulate this wherever a CI runner can; where it cannot, this
checklist is run by hand on real hardware before a release, and its
result goes in the release notes.

## What the tests cover

| Backend | Device plugged in and out | Default changed | Service restarted |
|---|---|---|---|
| PipeWire | a second client adds and removes nodes (`test_pipewire`) | yes, by that client | the daemon stopped and started (`test_pipewire_restart`) |
| PulseAudio | a sink loaded and unloaded with `pactl` (`test_pulse`) | yes, with `pactl` | the server stopped and started (`test_pulse`) |
| ALSA | made-up endpoint lists, and card nodes a test directory gains and loses (`test_alsa_hotplug`) | not reported by ALSA | none (no service) |
| Core Audio | an aggregate device over BlackHole made and destroyed (`test_coreaudio`) | yes, the system default set to it and back | `coreaudiod` killed under a running stream (`test_coreaudio`, macOS cell) |
| WASAPI | the notification callbacks called by the test; streams moved between the endpoints there are (`test_wasapi`) | the same | not tested |

After `coreaudiod` restarts on the CI's BlackHole, a lone stream runs
three to five times fast until another client starts the device; on
real hardware, step 4 below checks the rate by ear.

The WASAPI tests run under Wine in development and skip on the CI's
Windows runner, which has no audio endpoint. Wine does not enforce COM
apartments.

## By hand, before a release

Use a build with tests (`ctest` passes first). Run `sample_devices
600` (samples/devices.c): it lists the devices, plays a quiet tone on
the default output, and prints every notification with its kind,
device and stream, the stream's latency when it moves, and its
underrun count when it grows.

On each platform, with two outputs at least (built-in speakers and a
USB or Bluetooth headset):

1. Plug the headset in. Expect `maud_notifyDeviceAdded`, then, if the
   system makes it the default, `maud_notifyDefaultChanged` and
   `maud_notifyStreamMoved` for the streams on the default. The tone
   plays on the headset without a gap longer than one period plus the
   device's start.
2. Unplug it. Expect `maud_notifyDeviceRemoved`; streams that followed
   the default move back (`maud_notifyStreamMoved`); a stream opened
   on the headset itself reports `maud_notifyStreamSuspended` with
   `maud_suspendDeviceLost`, never a crash or a silent stop.
3. Change the default in the system's settings. Expect
   `maud_notifyDefaultChanged` and the moves; a stream opened on a
   named device rather than the default stays where it is.
4. Restart the audio service while streams run:
   - Windows: `net stop audiosrv` and `net start audiosrv` as
     administrator.
   - macOS: `sudo killall coreaudiod`.
   - Linux: `systemctl --user restart pipewire pipewire-pulse`, or
     `pulseaudio -k`.

   Expect the devices removed and added again. Streams on a default
   report suspended and then resumed; none crashes, and the tone keeps
   its pitch (a stream running fast after the restart is a fault).
5. Connect a Bluetooth headset and switch it between its music and
   call profiles (open a capture stream to force the call profile).
   Expect the format change reported (`maud_notifyStreamFormatChanged`)
   or a move, and the latency the stream reports to grow by the
   Bluetooth delay.
6. On Windows, run steps 1 to 4 from a host whose main thread called
   `OleInitialize` first (a single-threaded apartment), as a window
   with drag and drop does.
7. On Linux, with no sound server running (`systemctl --user stop
   pipewire pipewire-pulse`, or no PulseAudio), so that ALSA opens the
   card itself: run `sample_devices 600`, which must list the card's
   endpoints, then suspend the process for a second (`kill -STOP`,
   then `kill -CONT`). Expect the tone back without a stuck stream,
   and an underrun counted; then unplug a USB card under a stream and
   expect `maud_suspendDeviceLost`. The tests' ALSA runs through
   PipeWire's plugin, which absorbs a stall without an xrun and has no
   cards, so this is where ALSA's recovery and card scan run.

Record for each step: the platform and version, the devices, the
notifications seen, and anything else heard (a click, a gap, a stuck
stream).
