# Device checklist

Devices come and go, defaults move, sound servers restart, and browsers change their minds. The tests drive these changes wherever a machine can host the platform ([Test environments](test-environments.md)); what they cannot drive is checked by hand on real hardware before a release, and the result goes in the release pull request.

## What the tests drive

| Backend | Device in and out | Default changed | Service restarted | Stream ended by the platform |
|---|---|---|---|---|
| PipeWire | a second client adds and removes nodes | by that client | the daemon stopped and started; devices keep their ids, streams wait and resume; past ten seconds the devices go | a playing and a stopped stream's node destroyed, both rebuilt |
| PulseAudio | a sink loaded and unloaded with `pactl` | with `pactl` | as PipeWire | as PipeWire |
| ALSA | made-up endpoint lists, card nodes a directory gains and loses | not reported by ALSA | no service | the default device's stream ended at its server, the device recovered |
| Core Audio | an aggregate device over BlackHole made and destroyed | the system default set to it and back | `coreaudiod` killed under a running stream | not driven; the backend listens for the device's loss and the server's restart |
| WASAPI (under Wine) | the notification callbacks called by the test, streams moved between the endpoints there are | the same | not on Windows | a stream whose thread failed opened again on a later drain, with growing waits |
| Web | the page's device list changed | a `default` entry of another group with a `devicechange` | no service | a track stopped with its `ended` event; a sink the browser refuses |

## By hand, before a release

Build with tests and run them first. Then run `sample_devices 600 capture`: it lists the devices, plays a quiet tone on the default output, captures the default input, and prints every notification with its kind, device, stream and reason number, the stream's latency when it moves by more than 100 ms, its underrun count when it grows, and each second the frames captured and their peak. On the web, serve the build's directory from `localhost` (`python3 -m http.server`), open `sample_devices.html`, allow the microphone and click: it runs ten minutes with the capture.

On each platform, with at least two outputs (built-in speakers and a USB or Bluetooth headset):

1. **Plug the headset in.** Expect `device added`; if the system makes it the default, `default changed` and `stream moved` for the streams on the default. The tone moves to the headset without a gap longer than a period plus the device's start.
2. **Unplug it.** Expect `device removed`. Streams that followed the default move back. Plug it in again, run `sample_devices 600 --device <key>` with the headset's key from the list, and unplug it: that stream, made on the headset itself, reports `stream suspended` with reason 1, `maud_suspendDeviceLost`, never a crash or a silent stop.
3. **Change the default in the system's settings.** Expect `default changed` and the moves; a stream made on a named device stays where it is.
4. **Restart the audio service while streams run**: on Windows `net stop audiosrv` and `net start audiosrv` as administrator; on macOS `sudo killall coreaudiod`; on Linux `systemctl --user restart pipewire pipewire-pulse`, or `pulseaudio -k`. Expect the devices removed and added again, or kept; streams on a default suspended, then resumed; nothing crashes, and the tone keeps its pitch (a stream running fast after the restart is a fault).
5. **Switch a Bluetooth headset between its music and call profiles** (open a capture stream to force the call profile). Expect the format change (`stream format changed`) or a move, and the stream's latency growing by the Bluetooth delay.
6. **On Windows, repeat steps 1 to 4 from a host whose main thread called `OleInitialize` first**, a single-threaded apartment, as a window with drag and drop has.
7. **On Linux, with no sound server running** (`systemctl --user stop pipewire.socket pipewire-pulse.socket pipewire pipewire-pulse wireplumber`, or `pulseaudio.socket pulseaudio` with PulseAudio; the sockets too, or the next client starts the server again), so that ALSA opens the card itself: `sample_devices 600` must list the card's endpoints, each with its `hw:` key. Play on one, `sample_devices 600 --device hw:CARD=<id>,DEV=0` with the key the list printed: on a desktop, ALSA's default PCM is the sound server's plugin, which plays nothing without the server. Stop the process for a second (`kill -STOP`, then `kill -CONT`): the tone comes back, an underrun is counted, the stream is not stuck. Then play on a USB card's endpoint the same way and unplug the card: expect `stream suspended` with reason 1, `maud_suspendDeviceLost`. A stream on the `default` PCM cannot report the loss, since that device never leaves the list. Start the sound server again after.
8. **Loopback**, where the platform has it (PipeWire, PulseAudio, WASAPI, the web): open a loopback stream on the default output while the tone plays and expect the tone in its blocks at its level. On Windows, stop every sound: its blocks pause, and come back with the next sound. Unplug the output it captures: one made on that output reports `maud_suspendDeviceLost`, one on the default moves with it. On the web, in each browser, share a tab with audio and press the browser's "Stop sharing": the stream reports `maud_suspendPermission`; sharing a window, or using a browser that shares no audio, leaves it there from the start.
9. **The web, in Chrome, Firefox and Safari**, with the page playing and capturing:
   - Change the system's default microphone. Expect `route changed` for the input's Default, the capture suspended with `maud_suspendPermission` and resumed, and its peak following the new microphone. Change the default output: `route changed` for the output's Default and the tone on the new output.
   - Unplug the microphone the capture is on, or withdraw the page's microphone permission in the address bar: the capture is suspended with `maud_suspendPermission`; with another microphone left, it resumes on it.
   - Where outputs are listed (Chrome), plug an output in and out: `device added` and `device removed`, the tone staying on the default.

Record, for each step: the platform and its version, the devices, the notifications seen, and anything heard (a click, a gap, a stuck stream).
