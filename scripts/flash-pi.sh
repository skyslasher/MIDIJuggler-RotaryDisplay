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

echo "Uploading firmware (env pi-serial)..."
pio run -e pi-serial -t upload

echo "Starting midijuggler..."
sudo systemctl start midijuggler 2>/dev/null || true
echo "Done."
