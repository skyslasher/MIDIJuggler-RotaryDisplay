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
| `elecrow128-wifi` | WiFi/OSC only |

## Build

For **USB to the Mac** (recommended first):

```bash
pio run -e elecrow128-serial -t upload --upload-port /dev/cu.usbmodemXXXX
```

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

- **Rotate** encoder: edit BPM (blinks), confirm with press or revert after 5 s
- **Press** without rotation: start/stop transport
- **Swipe** on settings row: Klick / Puls (local) / Intervall
- **Tap** settings row: toggle current page
