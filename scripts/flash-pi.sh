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
    echo "  $p"
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
      echo "  $p"
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

echo "Upload port: $PORT"
if [[ ! -e "$PORT" ]]; then
  echo "WARNING: $PORT not found."
  echo "Available Espressif ports:"
  list_espressif_ports | sed 's/^/  /' || true
  echo ""
  echo "All serial devices (pio device list):"
  pio device list 2>/dev/null || true
  exit 1
fi

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

echo "Uploading firmware (env pi-both) to $PORT..."
# display_ui.cpp #ifdefs depend on RotaryUi.h; force recompile after header export/patch.
rm -f .pio/build/pi-both/src/display_ui.cpp.o

# platformio.ini pins encoder 1 and previously used wait_for_upload_port=true.
# That hook replaces UPLOAD_PORT with the first reconnecting serial device, so a
# CLI --upload-port alone still flashed encoder 1 when two boards were connected.
# Session override loads after platformio.ini so upload_port/monitor_port stay pinned.
OVERRIDE="$(mktemp "${TMPDIR:-/tmp}/flash-pi-override.XXXXXX.ini")"
WRAPPER="$(mktemp "${TMPDIR:-/tmp}/flash-pi-wrapper.XXXXXX.ini")"
UPLOAD_LOG="$(mktemp "${TMPDIR:-/tmp}/flash-pi-upload.XXXXXX.log")"
cleanup() {
  rm -f "$OVERRIDE" "$WRAPPER" "$UPLOAD_LOG"
}
trap cleanup EXIT

cat > "$OVERRIDE" <<EOF
[env:pi-both]
upload_port = ${PORT}
monitor_port = ${PORT}
board_upload.wait_for_upload_port = false
EOF

cat > "$WRAPPER" <<EOF
[platformio]
extra_configs =
  ${ROOT}/platformio.ini
  ${OVERRIDE}
EOF

set +e
pio run -c "$WRAPPER" -e pi-both -t upload \
  --upload-port "$PORT" \
  --monitor-port "$PORT" \
  -v 2>&1 | tee "$UPLOAD_LOG"
UPLOAD_STATUS=${PIPESTATUS[0]}
set -e

if [[ $UPLOAD_STATUS -ne 0 ]]; then
  echo "ERROR: Upload failed (exit $UPLOAD_STATUS)." >&2
  exit "$UPLOAD_STATUS"
fi

if grep -Fq "$PORT" "$UPLOAD_LOG"; then
  echo "Verified upload command referenced $PORT"
else
  echo "WARNING: Could not confirm $PORT in verbose upload log; check output above."
fi

echo "Starting midijuggler..."
sudo systemctl start midijuggler 2>/dev/null || true
echo "Done."
