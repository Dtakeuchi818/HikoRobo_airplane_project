"""Mock UDP server integration tests; run with pytest + httpx installed."""
from __future__ import annotations

import socket
import threading

from fastapi.testclient import TestClient

from hikorobo_gcs.server.app import app


class MockXiao:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self.rate = 100
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            try:
                payload, addr = self.sock.recvfrom(512)
            except socket.timeout:
                continue
            except OSError:
                break
            command = payload.decode("ascii").strip()
            if command == "PING":
                answer = "OK PONG"
            elif command == "GET_STATUS":
                answer = f"OK STATUS READY RATE={self.rate}"
            elif command == "GET_WIFI":
                answer = "OK WIFI CONNECTED IP=127.0.0.1"
            elif command == "GET_RATE":
                answer = f"OK RATE {self.rate}"
            elif command.startswith("SET_RATE "):
                self.rate = int(command.split()[1])
                answer = f"OK RATE {self.rate}"
            else:
                answer = "ERR UNKNOWN_COMMAND"
            self.sock.sendto((answer + "\n").encode("ascii"), addr)

    def close(self):
        self.stop.set()
        self.sock.close()
        self.thread.join(timeout=1)


def test_commands():
    mock = MockXiao()
    try:
        client = TestClient(app)
        base = {"host": "127.0.0.1", "port": mock.port, "timeout_sec": 0.5}
        endpoints = [
            ("/api/ping", "OK PONG"),
            ("/api/get-status", "OK STATUS READY RATE=100"),
            ("/api/get-wifi", "OK WIFI CONNECTED IP=127.0.0.1"),
            ("/api/get-rate", "OK RATE 100"),
        ]
        for path, expected in endpoints:
            resp = client.post(path, json=base)
            assert resp.status_code == 200, resp.text
            assert resp.json()["reply"] == expected
            assert resp.json()["rtt_ms"] >= 0
        resp = client.post("/api/set-rate", json={**base, "rate": 250})
        assert resp.status_code == 200
        assert resp.json()["reply"] == "OK RATE 250"
        resp = client.post("/api/get-rate", json=base)
        assert resp.json()["reply"] == "OK RATE 250"
        assert client.get("/").status_code == 200
        assert "HikoRobo GCS" in client.get("/").text
        assert client.get("/static/app.js").status_code == 200
        assert client.get("/api/health").json() == {"status": "ok"}
        assert client.get("/api/hello").json() == {"message": "Hello, HikoRobo!"}
    finally:
        mock.close()


def test_invalid_rate():
    client = TestClient(app)
    for invalid in (0, 501, -1, 1.5):
        resp = client.post("/api/set-rate", json={"host": "127.0.0.1", "rate": invalid})
        assert resp.status_code == 422


def test_timeout():
    unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    unused.bind(("127.0.0.1", 0))
    port = unused.getsockname()[1]
    unused.close()
    response = TestClient(app).post("/api/ping", json={"host": "127.0.0.1", "port": port, "timeout_sec": 0.15})
    assert response.status_code in (502, 504)
