#!/usr/bin/env bash
# Flash rotary display from Pi 5. Stops MIDIJuggler so the USB port is free.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${ROTARY_UPLOAD_PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_14:C1:9F:26:72:48-if00}"

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
echo "Uploading firmware (env pi-serial)..."
pio run -e pi-serial -t upload

echo "Starting midijuggler..."
sudo systemctl start midijuggler 2>/dev/null || true
echo "Done."
