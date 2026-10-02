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

The server part may be an IP address, e.g. `pextest1@127.0.0.1`; picall then
lets Pulse connect to a bare IP. A certificate is rarely valid for an IP, so
add `--insecure` unless yours is.

Press Ctrl-C to hang up. picall also exits by itself when the far end ends the
call.

| Option | Meaning |
| ------ | ------- |
| `--pin PIN` | Conference PIN (or `PICALL_PIN` in the environment). |
| `--name NAME` | Display name shown to others (default `picall`). |
| `--camera TEXT` | Use the first camera whose name contains `TEXT`. |
| `--ca-bundle PATH` | CA certificates for TLS (default `/etc/ssl/certs/ca-certificates.crt`). |
| `--i2c-bus PATH` | I2C bus of the PiRacer (default `/dev/i2c-1`). |
| `--no-piracer` | Do not open the I2C bus; car commands are ignored. |
| `--insecure` | Skip TLS certificate and host name checks. Needed for a self-signed MCU certificate. Lab use only. |
| `--dev-vmr [conference]` | Dial the lab VMR `192.168.1.38` (conference `pextest1` unless given) with `--insecure`. Lab use only. |
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

## Driving a PiRacer (Donkey car chassis)

On a Waveshare PiRacer Pro, picall turns data channel messages into I2C writes
to the PCA9685 PWM chip (0x40, 60 Hz): steering servo on channel 0, ESC on
channel 1. The drivers are a C++ port of `.piracer-i2c`, kept in
`src/pca9685.*`, `src/ina219.*` and `src/i2c_bus.*`; `src/piracer.*` is the
controller.

Setup: enable I2C (`sudo raspi-config nonint do_i2c 0`), add your user to the
`i2c` group, and stop donkeycar and `picard_display`, which also write to the
bus. If `/dev/i2c-1` cannot be opened picall prints why and carries on as a
plain video caller.

At start-up the servo goes to 1550 us (centre) and the ESC is held at neutral
(1500 us) for 1 s so it arms; throttle commands in that second are ignored.

The 128x32 OLED (0x3C, `src/ssd1306.*`) shows picall's `--name`: 10 characters
per line in large text, or small text for names over 20 characters. Characters
outside ASCII show as `?`. The panel is cleared when picall exits. If it
is missing, picall logs that and goes on.

Commands, sent as `<picall's --name>: <text>` (a leading `/` is accepted):

| Message text | Effect |
| ------------ | ------ |
| `piracer servo 0 1550` | Servo channel 0 to 1550 us (500..2500). |
| `piracer throttle --ms 5000 --channel 1 -0.2` | ESC `-1..1` (negative = reverse) for `--ms` (default 1000, max 60000), then neutral. `--channel` defaults to 1. |
| `piracer steer -0.5` | Servo to 1550 + value x 400 us. |
| `piracer power` | Reads the INA219 and logs bus voltage, current, power and battery %. |

Keys (`KEY_<KEY>_PRESS`; releases are ignored):

| Key | Effect |
| --- | ------ |
| `UP`, `W` | Throttle on channel 1 +0.1 (at most +0.9) |
| `DOWN`, `S` | Throttle on channel 1 -0.1 (at least -0.9). From exactly 0 it first holds -0.9 for 300 ms (`reverse_kick_time`) to engage the ESC's reverse, then steps to -0.1; further presses are ignored meanwhile, and `UP`/`W` abandons it and goes forward from 0. |
| `LEFT`, `A` | Servo 0 +150 us (at most 2300) |
| `RIGHT`, `D` | Servo 0 -150 us (at least 800) |
| `SHIFT` held | `LEFT`/`A` and `RIGHT`/`D` move the servo by 10 us instead of 150. Needs the sender to send `KEY_SHIFT_PRESS` and `KEY_SHIFT_RELEASE` (see `dcsctp_control.md`). A lost release is forgotten after the 5 s safety timer. |

**Safety timer:** if no valid command or key message (a key release counts)
arrives for 5 s, every throttle channel that is not at neutral is set back to
neutral. This also cuts a `--ms` throttle longer than 5 s. Throttle also returns
to neutral when picall exits or the call ends. A command that is rejected
(bad range, ESC arming) does not restart the timer. Commands are handled on
picall's main thread, so `piracer power` pauses the timers for about 40 ms.

`ctest --test-dir build/demos/picall` runs `piracer_test`, which checks the
register writes against a mock bus and needs neither hardware nor Pulse.

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

Most of picall is in [`src/main.cpp`](src/main.cpp). `main()` follows the six
steps in the header comment. `pick_camera()` is doppler's
`cache_default_camera()` with a name filter added. The `alias@server` split
copies pexninja: the server is the part after the last `@`, and the full alias
is sent as the conference name.
