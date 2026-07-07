# MIDIJuggler-RotaryDisplay

Firmware for the Elecrow CrowPanel 1.28-inch HMI ESP32 rotary display. Remote
control surface for the [MIDIJuggler](https://github.com/skyslasher/MIDIJuggler)
master clock over **USB serial** and/or **WiFi OSC**.

Requires a running MIDIJuggler instance with the `rotary_display` module enabled
(see MIDIJuggler `configs/rotary_display.toml` and `docs/rotary_display.md`).

## Hardware

- ESP32-S3, 240×240 round IPS display, capacitive touch, rotary encoder, WS2812 ring

## Transports

| Build env | Transport |
|-----------|-----------|
| `elecrow128` (default) | USB serial + WiFi/OSC |
| `elecrow128-serial` | USB serial only |
| `pi-serial` | USB serial only (Pi 5, fixed by-id port) |
| `elecrow128-wifi` | WiFi/OSC only |

## Build

For **USB to the Mac** (recommended first):

```bash
pio run -e elecrow128-serial -t upload --upload-port /dev/cu.usbmodemXXXX
```

For **USB on Raspberry Pi 5** (encoder stays on the Pi; stop MIDIJuggler first):

```bash
sudo systemctl stop midijuggler
pio run -e pi-serial -t upload
sudo systemctl start midijuggler
```

Or use the helper script (same steps, frees the serial port automatically):

```bash
./scripts/flash-pi.sh
```

If upload fails with **"The chip stopped responding"**, ensure MIDIJuggler is stopped,
unplug/replug USB once, then retry. The `pi-serial` env uses a slower upload speed and
`esp-builtin` over USB-JTAG for reliability. As a fallback:

```bash
sudo systemctl stop midijuggler
pio run -e pi-serial -t upload --upload-speed 115200
```

The `pi-serial` environment sets `upload_port` / `monitor_port` in
`platformio.ini` at the repo root. Adjust the by-id path there if your
device ID differs (`ls /dev/serial/by-id/`).

For **WiFi/OSC** (opens a setup hotspot `RotaryDisplay-Setup` on first boot):

```bash
pio run -e elecrow128-wifi -t upload --upload-port /dev/cu.usbmodemXXXX
```

Default `elecrow128` enables **both** USB and WiFi; on first boot it may sit in WiFi
setup for up to two minutes before the UI appears if no saved network exists.

```bash
pio run -e elecrow128
pio run -t upload
pio device monitor
```

## USB serial protocol

Device → MIDIJuggler:

```
hello
bpm 128.0
start_stop
click_toggle
interval quarter
```

MIDIJuggler → device:

```
sync 120.0 1 0 quarter
beat 1.0
```

Configuration over serial:

```
host midijuggler.local
port 9000
transport serial
transport wifi
transport both
```

## UI

Five swipeable pages: BPM (default), Audio-Klick, Puls, Intervall, Netzwerk.
On boot a splash screen is shown for 2 seconds.

- **Rotate** encoder on BPM page: edit BPM, confirm with press or revert after 5 s
- **Press** without rotation on BPM page: start/stop transport
- **Swipe** left/right: change page (wraps around)
- **Tap** on BPM page: tap tempo
- **Press** on Klick/Puls pages: toggle setting
- **Rotate** on Intervall page: edit interval with 5 s confirm/revert

### Screen layout (LGFXScreenBuilder)

The boot splash uses [LGFXScreenBuilder](https://tanakamasayuki.github.io/LGFXScreenBuilder/)
via `include/RotaryUi.h` (included only from `src/display_ui.cpp`). Profile index 2
is **240×240** for the Elecrow panel (`Profile::Rotary`). The boot scene defines
logo panel, title, and subtitle — no manual overlay drawing.

The five main pages (BPM, Klick, Puls, Intervall, Netzwerk) are drawn manually in
`src/display_ui.cpp`. Only the boot scene is rendered through LGFXScreenBuilder.

To redesign the boot screen visually:

1. Open the [authoring tool](https://tanakamasayuki.github.io/LGFXScreenBuilder/) (target: **LovyanGFX**)
2. Add or edit a **240×240** profile and the `Boot` scene (parts: logo, title, subtitle)
3. Export `.h` and replace `include/RotaryUi.h`
4. If the export uses brace-initialized descriptor arrays, keep the factory-helper
   pattern in the current header (LGFXScreenBuilder 0.2.x + ESP32 GCC) or regenerate
   once the tool emits compatible init code
5. Rebuild: `pio run -e elecrow128-serial -t upload`

