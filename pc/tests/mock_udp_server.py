"""Simple LOCAL-ONLY UDP emulator for end-to-end host tests (not real ESP-IDF)."""
from __future__ import annotations

import argparse
import socket


def serve(host: str = "127.0.0.1", port: int = 5005) -> None:
    rate = 100
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind((host, port))
        print(f"Mock XIAO listening on {host}:{port}; Ctrl+C to stop", flush=True)
        while True:
            data, address = server.recvfrom(512)
            try:
                cmd = data.decode("ascii").strip()
                if cmd == "PING":
                    reply = "OK PONG"
                elif cmd == "GET_WIFI":
                    reply = "OK WIFI CONNECTED IP=127.0.0.1"
                elif cmd == "GET_STATUS":
                    reply = f"OK STATUS READY RATE={rate}"
                elif cmd == "GET_RATE":
                    reply = f"OK RATE {rate}"
                elif cmd.startswith("SET_RATE "):
                    try:
                        new_rate = int(cmd.split(" ", 1)[1])
                        if not 1 <= new_rate <= 500:
                            reply = "ERR RATE_RANGE"
                        else:
                            rate = new_rate
                            reply = f"OK RATE {rate}"
                    except ValueError:
                        reply = "ERR BAD_ARGUMENT"
                elif cmd == "HELP":
                    reply = "OK COMMANDS PING GET_STATUS GET_RATE SET_RATE GET_WIFI HELP"
                else:
                    reply = "ERR UNKNOWN_COMMAND"
            except UnicodeDecodeError:
                reply = "ERR BAD_CHARACTER"
            server.sendto((reply + "\n").encode("ascii"), address)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=5005)
    args = parser.parse_args()
    serve(port=args.port)
