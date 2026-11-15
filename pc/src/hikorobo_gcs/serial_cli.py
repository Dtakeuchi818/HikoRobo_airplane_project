"""USB Serial/JTAG command test client for XIAO ESP32S3 Plus."""

from __future__ import annotations

import argparse
import sys
import time
from typing import Protocol


class SerialLike(Protocol):
    def write(self, data: bytes) -> int: ...
    def flush(self) -> None: ...
    def readline(self) -> bytes: ...


def exchange(device: SerialLike, command: str, timeout: float = 3.0) -> str:
    """Send one LF-terminated ASCII command and await its OK/ERR reply."""
    command = command.strip()
    if not command or "\n" in command or "\r" in command:
        raise ValueError("One non-empty command per line is required")
    if len(command) > 95:
        raise ValueError("Command must be 95 ASCII characters or fewer")
    data = (command + "\n").encode("ascii")
    device.write(data)
    device.flush()

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = device.readline()
        if not raw:
            continue
        decoded = raw.decode("ascii", errors="replace").strip()
        if decoded.startswith(("OK ", "ERR ")) or decoded in ("OK", "ERR"):
            return decoded
        if decoded:
            print(f"[other serial output] {decoded}", file=sys.stderr)
    raise TimeoutError(f"No reply for {command!r} within {timeout:g} s")


def self_test(device: SerialLike, timeout: float) -> bool:
    cases = [
        ("PING", "OK PONG"),
        ("GET_STATUS", "OK STATUS READY RATE="),
        ("SET_RATE 250", "OK RATE 250"),
        ("GET_RATE", "OK RATE 250"),
        ("SET_RATE 999", "ERR RATE_RANGE"),
        ("INVALID", "ERR UNKNOWN_COMMAND"),
        ("SET_RATE 100", "OK RATE 100"),
    ]
    success = True
    for command, expected in cases:
        try:
            received = exchange(device, command, timeout)
            passed = received.startswith(expected)
        except (TimeoutError, ValueError, OSError) as exc:
            received = str(exc)
            passed = False
        print(f"{'PASS' if passed else 'FAIL'}  {command:15s} -> {received}")
        success &= passed
    return success


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB serial device (e.g., /dev/ttyACM0 or COM3)")
    parser.add_argument("--list", action="store_true", help="List available serial ports")
    parser.add_argument("--self-test", action="store_true", help="Run predefined command tests")
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.error("pyserial is not installed. Install pc/pyproject.toml dependencies first.")

    if args.list:
        for port in list_ports.comports():
            print(f"{port.device}: {port.description}")
        return
    if not args.port:
        parser.error("--port is required unless --list is specified")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        kwargs = {"port": args.port, "baudrate": 115200, "timeout": 0.2, "write_timeout": 2.0}
        if sys.platform.startswith("linux"):
            kwargs["exclusive"] = True
        with serial.Serial(**kwargs) as device:
            # Ignore ROM boot text or earlier data; protocol has no unsolicited messages.
            time.sleep(0.3)
            device.reset_input_buffer()
            if args.self_test:
                if not self_test(device, args.timeout):
                    sys.exit(1)
                return
            print(f"Connected to {args.port}. Enter HELP/PING/GET_STATUS/GET_RATE/SET_RATE 250.")
            print("Type quit or exit to close.")
            while True:
                try:
                    command = input("XIAO> ").strip()
                except (EOFError, KeyboardInterrupt):
                    print()
                    break
                if command.lower() in ("quit", "exit"):
                    break
                if not command:
                    continue
                try:
                    print(exchange(device, command, args.timeout))
                except (TimeoutError, ValueError, UnicodeEncodeError) as exc:
                    print(f"ERROR: {exc}", file=sys.stderr)
    except serial.SerialException as exc:
        parser.exit(1, f"Serial port error: {exc}\n")


if __name__ == "__main__":
    main()
