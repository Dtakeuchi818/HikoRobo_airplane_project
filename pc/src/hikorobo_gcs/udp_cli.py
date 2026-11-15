"""UDP command client for XIAO ESP32S3 Plus (Ubuntu hotspot, port 5005)."""
from __future__ import annotations

import argparse
import csv
import ipaddress
import socket
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_PORT = 5005


def validate_command(command: str) -> bytes:
    """Exactly one ASCII command (max 95 bytes), optionally padded with spaces."""
    if "\n" in command or "\r" in command:
        raise ValueError("Only one line per UDP datagram is supported")
    command = command.strip()
    if not command:
        raise ValueError("Command must not be empty")
    try:
        raw = command.encode("ascii")
    except UnicodeEncodeError as e:
        raise ValueError("Command must use ASCII") from e
    if len(raw) > 95 or any(c < 32 or c > 126 for c in raw):
        raise ValueError("Command must be 1-95 printable ASCII characters")
    return raw + b"\n"


def exchange(sock: socket.socket, peer: tuple[str, int], command: str,
             timeout: float = 2.0) -> tuple[str, float]:
    """Send request, wait for a response from the SAME IP/port, return reply + RTT ms."""
    if timeout <= 0:
        raise ValueError("timeout must be > 0")
    packet = validate_command(command)
    start = time.monotonic()
    sock.sendto(packet, peer)
    deadline = start + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"Timeout waiting for {command!r}")
        sock.settimeout(remaining)
        try:
            data, sender = sock.recvfrom(512)
        except socket.timeout as exc:
            raise TimeoutError(f"Timeout waiting for {command!r}") from exc
        if sender != peer:
            continue  # Do not accept a reply from another device.
        msg = data.decode("ascii", errors="replace").strip()
        if msg.startswith(("OK ", "ERR ")) or msg in ("OK", "ERR"):
            return msg, (time.monotonic() - start) * 1000.0
        # Extra messages can occur; skip them until the deadline.


def discover(sock: socket.socket, broadcast: str, port: int,
             timeout: float) -> tuple[str, int]:
    """Broadcast PING and return the first responding XIAO (or mock server)."""
    ipaddress.IPv4Address(broadcast)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.sendto(b"PING\n", (broadcast, port))
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("No XIAO replied to broadcast PING")
        sock.settimeout(remaining)
        try:
            data, sender = sock.recvfrom(512)
        except socket.timeout as exc:
            raise TimeoutError("No XIAO replied to broadcast PING") from exc
        if sender[1] == port and data.strip() == b"OK PONG":
            return sender


def self_test(sock: socket.socket, peer: tuple[str, int], timeout: float) -> bool:
    tests = [
        ("PING", "OK PONG"),
        ("GET_WIFI", "OK WIFI CONNECTED IP="),
        ("GET_STATUS", "OK STATUS READY RATE="),
        ("SET_RATE 250", "OK RATE 250"),
        ("GET_RATE", "OK RATE 250"),
        ("SET_RATE 999", "ERR RATE_RANGE"),
        ("INVALID", "ERR UNKNOWN_COMMAND"),
        ("SET_RATE 100", "OK RATE 100"),
    ]
    all_good = True
    for command, expected in tests:
        try:
            reply, rtt = exchange(sock, peer, command, timeout)
            passed = reply.startswith(expected)
            print(f"{'PASS' if passed else 'FAIL'}  {command:14s} -> {reply} ({rtt:.1f} ms)")
        except (TimeoutError, OSError, ValueError) as e:
            passed = False
            print(f"FAIL  {command:14s} -> {e}")
        all_good = all_good and passed
    return all_good


def benchmark(sock: socket.socket, peer: tuple[str, int], timeout: float,
              count: int, interval: float, output_csv: Path | None) -> bool:
    if count < 1 or interval < 0:
        raise ValueError("count must be positive and interval >= 0")
    records: list[dict[str, str | int | float]] = []
    measured: list[float] = []
    for i in range(count):
        timestamp = datetime.now(timezone.utc).isoformat()
        try:
            reply, rtt = exchange(sock, peer, "PING", timeout)
            success = reply == "OK PONG"
        except (TimeoutError, OSError) as e:
            reply, rtt, success = str(e), None, False
        if success and rtt is not None:
            measured.append(rtt)
        records.append({
            "sequence": i,
            "pc_utc": timestamp,
            "success": int(success),
            "rtt_ms": "" if rtt is None else round(rtt, 3),
            "reply": reply,
        })
        if i + 1 != count:
            time.sleep(interval)

    ok = len(measured)
    print(f"sent={count} replies={ok} unanswered={count - ok} "
          f"unanswered_rate={100 * (count - ok) / count:.2f}%")
    if measured:
        sorted_ms = sorted(measured)
        # nearest rank p95, deterministic for small test sample sizes
        p95 = sorted_ms[max(0, (95 * len(sorted_ms) + 99) // 100 - 1)]
        print(f"RTT milliseconds: min={min(measured):.2f}, median={statistics.median(measured):.2f}, "
              f"p95={p95:.2f}, max={max(measured):.2f}")
    if output_csv is not None:
        output_csv.parent.mkdir(parents=True, exist_ok=True)
        with output_csv.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(records[0]))
            w.writeheader()
            w.writerows(records)
        print(f"CSV saved: {output_csv}")
    return ok == count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    targets = parser.add_mutually_exclusive_group(required=True)
    targets.add_argument("--host", help="XIAO IP address, or 127.0.0.1 for mock server")
    targets.add_argument("--discover", action="store_true", help="Find XIAO using broadcast PING")
    parser.add_argument("--broadcast", default="10.42.0.255", help="AP subnet broadcast address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=2.0)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--self-test", action="store_true")
    modes.add_argument("--benchmark", type=int, metavar="N", help="Run N PING requests")
    parser.add_argument("--interval", type=float, default=0.05, help="Delay after each PING (s)")
    parser.add_argument("--csv", type=Path, help="Benchmark CSV path")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.timeout <= 0:
        parser.error("port must be 1..65535 and timeout must be > 0")
    if args.benchmark is not None and (args.benchmark < 1 or args.interval < 0):
        parser.error("benchmark must be >= 1 and interval >= 0")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            if args.discover:
                peer = discover(sock, args.broadcast, args.port, args.timeout)
                print(f"Discovered device: {peer[0]}:{peer[1]}")
            else:
                host_ip = socket.gethostbyname(args.host)
                peer = (host_ip, args.port)
            if args.self_test:
                if not self_test(sock, peer, args.timeout):
                    raise SystemExit(1)
                return
            if args.benchmark is not None:
                if not benchmark(sock, peer, args.timeout, args.benchmark,
                                 args.interval, args.csv):
                    raise SystemExit(1)
                return
            print(f"UDP peer {peer[0]}:{peer[1]}. Enter PING, GET_WIFI, GET_RATE, SET_RATE 250, etc.")
            print("Type quit or exit to close.")
            while True:
                try:
                    cmd = input("XIAO-UDP> ")
                except (KeyboardInterrupt, EOFError):
                    print()
                    break
                if cmd.strip().lower() in ("exit", "quit"):
                    break
                if not cmd.strip():
                    continue
                try:
                    answer, rtt_ms = exchange(sock, peer, cmd, args.timeout)
                    print(f"{answer}  [{rtt_ms:.1f} ms]")
                except (TimeoutError, ValueError, OSError) as e:
                    print(f"ERROR: {e}", file=sys.stderr)
        except (TimeoutError, OSError) as e:
            parser.exit(1, f"Network error: {e}\n")


if __name__ == "__main__":
    main()
