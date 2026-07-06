#!/usr/bin/env python3
"""Manual integration helper for the rotary display serial protocol."""

from __future__ import annotations

import argparse
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--send", default="hello", help="Line to send to the device")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("Install pyserial first: pip install pyserial", file=sys.stderr)
        return 1

    with serial.Serial(args.port, args.baud, timeout=1) as port:
        port.write((args.send + "\n").encode("utf-8"))
        while True:
            line = port.readline()
            if not line:
                break
            print(line.decode("utf-8", errors="replace").rstrip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
