# picall — a headless, video-only Pulse caller for Raspberry Pi

`picall` joins a Pexip Infinity conference from the command line and sends one
camera as the MAIN video. It attaches no microphone or speaker and shows no
video, so it runs on a Raspberry Pi 4 with just a camera. It is doppler
without the GUI: the same REST connect, with Pulse driving the camera itself.

```
 Pi camera ──libcamera──▶ V4L2 (libcamerify) ──▶ Pulse device session (MAIN video in)
                                                     │
                                                     ▼
                          pulse_connect_with_rest_async() ──▶ Pexip Infinity
```

## What it highlights

* `pulse_device_iterator_new()` / `pulse_device_session_connect_device()` —
  pick one camera by name and bind it to MAIN video.
* `pulse_connect_with_rest_async()` — join a conference from an
  `alias@server` string plus an optional PIN.
* Leaving out audio: no audio device session is connected, so the call has no
  sound in or out.

## Raspberry Pi 4 (Raspberry Pi OS trixie, 64-bit) setup

1. Install the arm64 Pulse packages (the `*_arm64.deb` files):

   ```bash
   sudo apt install ./libpexcommon_*_arm64.deb \
                    ./libpexpulse_*_arm64.deb  \
                    ./libpexpulse-dev_*_arm64.deb
   ```

2. Install the build tools and libcamera's V4L2 compatibility layer:

   ```bash
   sudo apt install cmake build-essential libcamera-v4l2
   ```

   Pulse reads cameras through V4L2. A CSI camera on a Pi 4 is only reachable
   through libcamera, so `libcamerify` (from `libcamera-v4l2`) is what makes it
   visible to Pulse.

3. Build only picall (no GLFW/OpenGL/ImGui needed):

   ```bash
   cmake -S . -B build -DBUILD_DOPPLER=OFF -DBUILD_GATEWAY=OFF -DBUILD_VIDEOWALL=OFF
   cmake --build build -j --target picall
   ```

## Run

Check that Pulse can see the camera first:

```bash
libcamerify ./build/run-picall.sh --list-devices
```

Then place the call:

```bash
libcamerify ./build/run-picall.sh meet.mikhail.baranov@nightly.pexip.com --pin 559949
```

To keep the PIN out of your shell history, set `PICALL_PIN` instead of
passing `--pin`.

Press Ctrl-C to hang up. picall also exits by itself when the far end ends the
call.

| Option | Meaning |
| ------ | ------- |
| `--pin PIN` | Conference PIN (or `PICALL_PIN` in the environment). |
| `--name NAME` | Display name shown to others (default `picall`). |
| `--camera TEXT` | Use the first camera whose name contains `TEXT`. |
| `--ca-bundle PATH` | CA certificates for TLS (default `/etc/ssl/certs/ca-certificates.crt`). |
| `--dev-vmr [conference]` | Dial the lab VMR `192.168.1.38` (conference `pextest1` unless given) with TLS peer and hostname verification off. Lab use only. |
| `--list-devices` | Print the cameras Pulse can see, then exit. |
| `-v` | Print all Pulse log output, not just warnings and errors. |

Exit codes: `0` normal hang-up, `1` the connect failed (wrong PIN, unreachable
server, ...), `2` bad arguments or no camera.

## Key control (app data channel)

picall joins the Pulse app data channel (SCTP stream 5) and logs key presses
another Pulse client sends to it, e.g. pexninja's "Send controls to" list. See
[`dcsctp_control.md`](../../dcsctp_control.md) for the protocol.

```
pexcart1: key UP pressed
pexcart1: key UP released
```

Only messages of the form `<picall's --name>: KEY_<KEY>_PRESS|RELEASE` are
logged. The message doesn't say who sent it. Needs Pulse 1.0.18611 or newer,
and the MCU must have `enable_app_datachannel` turned on.

## Troubleshooting

* **`STREAMON 22 (Invalid argument)` / camera named `unicam`**: picall was
  started without `libcamerify`. `unicam` is the raw sensor node, which only
  gives raw Bayer data that Pulse can't stream. Run it under `libcamerify`; the camera then
  shows up under its libcamera name.
* **`The server certificate failed validation`**: the arm64 Pulse build doesn't
  know where Debian keeps its CA certificates. picall passes
  `/etc/ssl/certs/ca-certificates.crt` by default; make sure the
  `ca-certificates` package is installed, or pass `--ca-bundle`.

## Code tour

Everything is in [`src/main.cpp`](src/main.cpp). `main()` follows the six
steps in the header comment. `pick_camera()` is doppler's
`cache_default_camera()` with a name filter added. The `alias@server` split
copies pexninja: the server is the part after the last `@`, and the full alias
is sent as the conference name.
