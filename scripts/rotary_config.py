#!/usr/bin/env python3
"""Push or read rotary display configuration over USB serial."""

from __future__ import annotations

import argparse
import sys
import time


def open_port(path: str, baud: int):
    try:
        import serial
    except ImportError:
        print("Install pyserial first: pip install pyserial", file=sys.stderr)
        raise SystemExit(1)
    return serial.Serial(path, baud, timeout=1)


def read_lines(port, timeout_s: float = 3.0) -> list[str]:
    deadline = time.monotonic() + timeout_s
    lines: list[str] = []
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip()
        if line:
            lines.append(line)
            if line in ("ok",) or line.startswith("err "):
                break
    return lines


def cmd_get(args: argparse.Namespace) -> int:
    with open_port(args.port, args.baud) as port:
        port.reset_input_buffer()
        port.write(b"config get\n")
        for line in read_lines(port, args.timeout):
            print(line)
    return 0


def cmd_apply(args: argparse.Namespace) -> int:
    with open_port(args.port, args.baud) as port:
        port.reset_input_buffer()
        for command in args.command:
            port.write((command + "\n").encode("utf-8"))
            for line in read_lines(port, args.timeout):
                print(f"> {command}")
                for response in line.split("\n"):
                    print(response)
                if line.startswith("err "):
                    return 1
        port.write(b"config apply\n")
        for line in read_lines(port, args.timeout):
            print("> config apply")
            print(line)
            if line.startswith("err "):
                return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    sub = parser.add_subparsers(dest="action", required=True)

    get_parser = sub.add_parser("get", help="Read current config")
    get_parser.set_defaults(func=cmd_get)

    apply_parser = sub.add_parser("apply", help="Stage commands then config apply")
    apply_parser.add_argument(
        "command",
        nargs="+",
        help='Config lines, e.g. "host midijuggler.local" "transport both"',
    )
    apply_parser.set_defaults(func=cmd_apply)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
