#!/usr/bin/env bash
# Flash rotary display from Pi 5. Stops MIDIJuggler so the USB port is free.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
  cat <<EOF
Usage: $0 [-l] [/dev/serial/by-id/...]

Flash pi-both firmware to one Elecrow encoder on the Pi.

Options:
  -l    List Espressif USB serial ports and exit

Port resolution (first match wins):
  1. CLI argument
  2. ROTARY_UPLOAD_PORT environment variable
  3. Single Espressif by-id port auto-detected
  4. platformio.ini default (encoder 1) when exactly zero Espressif ports match

When multiple Espressif devices are connected, pass the target port explicitly.
The pi-both env uses upload_protocol=esp-builtin (OpenOCD), which ignores
upload_port. This script pins the USB-JTAG adapter via upload_flags
"adapter serial …" extracted from the by-id path.
EOF
}

# Espressif CrowPanel boards expose USB-JTAG serial under /dev/serial/by-id/.
list_espressif_ports() {
  local ports=()
  shopt -s nullglob
  for entry in /dev/serial/by-id/usb-Espressif_*; do
    [[ -e "$entry" ]] || continue
    ports+=("$entry")
  done
  shopt -u nullglob
  if ((${#ports[@]})); then
    printf '%s\n' "${ports[@]}"
  fi
}

# by-id example:
# /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_14:C1:9F:26:72:48-if00
adapter_serial_from_port() {
  local port="$1"
  if [[ "$port" =~ _([0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5})-if00$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  if command -v udevadm >/dev/null 2>&1; then
    local serial
    serial="$(udevadm info -q property -n "$port" 2>/dev/null | sed -n 's/^ID_SERIAL_SHORT=//p' | head -1)"
    if [[ -n "$serial" ]]; then
      echo "$serial"
      return 0
    fi
  fi
  return 1
}

esptool_path() {
  local p="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
  if [[ -f "$p" ]]; then
    echo "$p"
    return 0
  fi
  return 1
}

read_device_mac() {
  local port="$1"
  local esptool
  esptool="$(esptool_path)" || return 1
  python3 "$esptool" --chip esp32s3 --port "$port" chip_id 2>/dev/null \
    | sed -n 's/^MAC: //p' | tail -1 | tr '[:lower:]' '[:upper:]'
}

normalize_mac() {
  echo "${1//:/}" | tr '[:lower:]' '[:upper:]'
}

PORT=""
LIST_ONLY=false

while getopts ":lh" opt; do
  case "$opt" in
    l)
      LIST_ONLY=true
      ;;
    h)
      usage
      exit 0
      ;;
    \?)
      echo "ERROR: Unknown option: -$OPTARG" >&2
      usage >&2
      exit 1
      ;;
  esac
done
shift $((OPTIND - 1))

if $LIST_ONLY; then
  echo "Espressif serial ports:"
  PORTS=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && PORTS+=("$line")
  done < <(list_espressif_ports)
  if ((${#PORTS[@]} == 0)); then
    echo "  (none found under /dev/serial/by-id/usb-Espressif_*)"
    echo ""
    echo "All serial devices (pio device list):"
    pio device list 2>/dev/null || true
    exit 0
  fi
  for p in "${PORTS[@]}"; do
    serial="$(adapter_serial_from_port "$p" 2>/dev/null || echo "?")"
    echo "  $p"
    echo "    adapter serial: $serial"
  done
  exit 0
fi

if [[ $# -ge 1 ]]; then
  PORT="$1"
elif [[ -n "${ROTARY_UPLOAD_PORT:-}" ]]; then
  PORT="$ROTARY_UPLOAD_PORT"
else
  PORTS=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && PORTS+=("$line")
  done < <(list_espressif_ports)
  if ((${#PORTS[@]} == 1)); then
    PORT="${PORTS[0]}"
    echo "Auto-selected upload port: $PORT"
  elif ((${#PORTS[@]} > 1)); then
    echo "ERROR: Multiple Espressif devices found; specify which encoder to flash:"
    for p in "${PORTS[@]}"; do
      serial="$(adapter_serial_from_port "$p" 2>/dev/null || echo "?")"
      echo "  $p  (serial $serial)"
    done
    echo ""
    echo "Example: $0 \"${PORTS[0]}\""
    echo "List ports: $0 -l"
    exit 1
  else
    PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_14:C1:9F:26:72:48-if00"
    echo "No Espressif by-id port detected; using platformio.ini default: $PORT"
  fi
fi

if [[ -z "$PORT" ]]; then
  echo "ERROR: No upload port specified." >&2
  usage >&2
  exit 1
fi

if [[ ! -e "$PORT" ]]; then
  echo "ERROR: $PORT not found." >&2
  echo "Available Espressif ports:"
  list_espressif_ports | sed 's/^/  /' || true
  echo ""
  echo "All serial devices (pio device list):"
  pio device list 2>/dev/null || true
  exit 1
fi

ADAPTER_SERIAL="$(adapter_serial_from_port "$PORT")" || {
  echo "ERROR: Could not determine USB-JTAG adapter serial from $PORT" >&2
  echo "Expected a by-id path like ..._14:C1:9F:26:72:48-if00" >&2
  exit 1
}

echo "Upload port: $PORT"
echo "Adapter serial: $ADAPTER_SERIAL"

echo "Stopping midijuggler..."
sudo systemctl stop midijuggler 2>/dev/null || true
sleep 1

if [[ -e "$PORT" ]] && command -v fuser >/dev/null 2>&1; then
  if fuser "$PORT" >/dev/null 2>&1; then
    echo "Releasing processes on $PORT..."
    sudo fuser -k "$PORT" 2>/dev/null || true
    sleep 2
  fi
fi

if ! esptool_path >/dev/null; then
  echo "Fetching esptool (first PlatformIO build)..."
  (cd "$ROOT" && pio run -e pi-both >/dev/null)
fi

echo "Pre-flash device check on $PORT..."
PRE_MAC="$(read_device_mac "$PORT")" || {
  echo "ERROR: esptool chip_id failed on $PORT (wrong port or device busy)." >&2
  exit 1
}
echo "Pre-flash MAC: $PRE_MAC"
if [[ "$(normalize_mac "$PRE_MAC")" != "$(normalize_mac "$ADAPTER_SERIAL")" ]]; then
  echo "ERROR: Port MAC ($PRE_MAC) does not match by-id serial ($ADAPTER_SERIAL)." >&2
  exit 1
fi

cd "$ROOT"

if [[ ! -f include/RotaryUi.h ]]; then
  echo "ERROR: include/RotaryUi.h fehlt."
  echo "Exportiere dein Layout aus LGFXScreenBuilder und speichere es als include/RotaryUi.h."
  echo "Für einen lokalen Dev-Stub: cp include/RotaryUi.h.repo-stub include/RotaryUi.h"
  exit 1
fi

if ! grep -q 'ROTARY_UI_HOME_DYNAMIC' include/RotaryUi.h; then
  echo "ERROR: include/RotaryUi.h ist nicht dein gepatchtes LGFXScreenBuilder-Export."
  echo "Die Datei wurde vermutlich durch 'git pull' mit der Repo-Platzhalter-Version überschrieben."
  echo "1. Export aus LGFXScreenBuilder erneut als include/RotaryUi.h speichern"
  echo "2. node scripts/patch-lgfxsb-export.mjs include/RotaryUi.h"
  echo "3. ./scripts/flash-pi.sh"
  exit 1
fi

if ! grep -q 'klickBgInactive' include/RotaryUi.h; then
  echo "ERROR: include/RotaryUi.h enthält keine Home-Chip-Parts (klickBgInactive)."
  exit 1
fi

if [[ -f include/RotaryUi.h ]] && command -v node >/dev/null 2>&1; then
  echo "Checking LGFXScreenBuilder export for ESP32 GCC..."
  node scripts/patch-lgfxsb-export.mjs include/RotaryUi.h
  node scripts/verify-rotary-ui.mjs include/RotaryUi.h
fi

echo "Uploading firmware (env pi-both) to adapter $ADAPTER_SERIAL..."
# display_ui.cpp #ifdefs depend on RotaryUi.h; force recompile after header export/patch.
rm -f .pio/build/pi-both/src/display_ui.cpp.o

# esp-builtin upload uses OpenOCD and ignores upload_port / --upload-port.
# Pin the USB-JTAG probe by adapter serial (from PlatformIO community / OpenOCD docs).
OVERRIDE="$(mktemp "${TMPDIR:-/tmp}/flash-pi-override.XXXXXX.ini")"
WRAPPER="$(mktemp "${TMPDIR:-/tmp}/flash-pi-wrapper.XXXXXX.ini")"
cleanup() {
  rm -f "$OVERRIDE" "$WRAPPER"
}
trap cleanup EXIT

cat > "$OVERRIDE" <<EOF
[env:pi-both]
upload_port = ${PORT}
monitor_port = ${PORT}
board_upload.wait_for_upload_port = false
upload_flags =
  -c
  adapter serial ${ADAPTER_SERIAL}
EOF

cat > "$WRAPPER" <<EOF
[platformio]
extra_configs =
  ${ROOT}/platformio.ini
  ${OVERRIDE}
EOF

pio run -c "$WRAPPER" -e pi-both -t upload \
  --upload-port "$PORT" \
  --monitor-port "$PORT"

sleep 2
echo "Post-flash device check on $PORT..."
POST_MAC="$(read_device_mac "$PORT")" || {
  echo "WARNING: post-flash chip_id failed; firmware may still have uploaded." >&2
  POST_MAC=""
}
if [[ -n "$POST_MAC" ]]; then
  echo "Post-flash MAC: $POST_MAC"
  if [[ "$(normalize_mac "$POST_MAC")" != "$(normalize_mac "$PRE_MAC")" ]]; then
    echo "ERROR: post-flash MAC changed ($POST_MAC vs $PRE_MAC); wrong device may have been flashed." >&2
    exit 1
  fi
  echo "Verified flash target: $POST_MAC on $PORT"
fi

echo "Starting midijuggler..."
sudo systemctl start midijuggler 2>/dev/null || true
echo "Done."
