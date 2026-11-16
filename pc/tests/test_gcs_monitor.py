"""GCS 0.4: integration against local mock UDP XIAO; no physical device needed."""
from __future__ import annotations

import socket
import threading
from unittest.mock import patch

from fastapi.testclient import TestClient
from hikorobo_gcs.server.app import app
from hikorobo_gcs.server.udp_commands import device_service


class MockXiao:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
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
                response = "OK PONG"
            elif command == "GET_STATUS":
                response = f"OK STATUS READY RATE={self.rate}"
            elif command == "GET_WIFI":
                response = "OK WIFI CONNECTED IP=127.0.0.1"
            elif command == "GET_RATE":
                response = f"OK RATE {self.rate}"
            elif command.startswith("SET_RATE "):
                self.rate = int(command.split(" ")[1])
                response = f"OK RATE {self.rate}"
            else:
                response = "ERR UNKNOWN_COMMAND"
            self.sock.sendto((response+"\n").encode(), addr)

    def close(self):
        self.stop.set()
        self.sock.close()
        self.thread.join(timeout=1)


def test_udp_commands_connect_and_quality():
    mock = MockXiao()
    try:
        with TestClient(app) as client:
            req = {"host": "127.0.0.1", "port": mock.port, "timeout_sec": 0.3}
            assert client.get("/api/health").json() == {"status": "ok"}
            assert client.get("/").status_code == 200
            assert "UDPネットワーク品質測定" in client.get("/").text
            assert client.get("/static/app.js").status_code == 200
            assert client.post("/api/connect", json=req).json()["reply"] == "OK PONG"
            assert client.post("/api/probe", json=req).json()["reply"] == "OK PONG"
            assert client.post("/api/ping", json=req).json()["reply"] == "OK PONG"
            assert client.post("/api/get-status", json=req).json()["reply"] == "OK STATUS READY RATE=100"
            assert client.post("/api/get-wifi", json=req).json()["reply"].startswith("OK WIFI")
            assert client.post("/api/get-rate", json=req).json()["reply"] == "OK RATE 100"
            assert client.post("/api/set-rate", json={**req,"rate":250}).json()["reply"] == "OK RATE 250"
            assert client.post("/api/get-rate",json=req).json()["reply"] == "OK RATE 250"
            result=client.post("/api/quality",json={**req, "count": 6, "interval_ms": 50})
            assert result.status_code == 200, result.text
            j=result.json()
            assert j["count"] == 6
            assert j["success_count"] == 6
            assert j["unanswered_percent"] == 0
            assert j["median_rtt_ms"] >= 0
            assert j["p95_rtt_ms"] >= j["median_rtt_ms"]
            assert len(j["samples"])==6
            assert client.post("/api/disconnect").json()["success"] is True
    finally:
        mock.close()


def test_quality_timeouts_and_validation():
    unused = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    unused.bind(("127.0.0.1", 0))
    port = unused.getsockname()[1]
    unused.close()
    with TestClient(app) as client:
        req={"host":"127.0.0.1","port":port,"timeout_sec":0.1}
        response=client.post("/api/quality",json={**req,"count":5,"interval_ms":50})
        assert response.status_code==200
        assert response.json()["success_count"]==0
        assert response.json()["unanswered_percent"]==100
        assert response.json()["median_rtt_ms"] is None
        assert client.post("/api/ping",json=req).status_code==504
        assert client.post("/api/quality",json={**req,"count":2}).status_code==422
        assert client.post("/api/quality",json={**req,"count":5,"transport":"usb"}).status_code==422
        assert client.post("/api/set-rate",json={**req,"rate":501}).status_code==422


def test_discovery_and_busy_lock():
    with TestClient(app) as client:
        with patch("hikorobo_gcs.server.udp_commands.udp_discover",return_value=("10.42.0.162",5005)):
            assert client.post("/api/discover",json={"broadcast":"10.42.0.255"}).json()["host"] == "10.42.0.162"
        device_service._lock.acquire()
        try:
            response=client.post("/api/ping", json={"host":"127.0.0.1"})
            assert response.status_code == 409
        finally:
            device_service._lock.release()


def test_usb_simulated(monkeypatch):
    """Fake device tests opening, keeping, communicating and closing serial port."""
    import sys
    import types

    opened = []

    class FakeSerial:
        def __init__(self, **kw):
            self.is_open=True
            self.input=[]
            self.written=[]
            opened.append(self)
        def write(self, raw):
            command=raw.strip().decode()
            self.written.append(command)
            if command == "PING": answer="OK PONG"
            elif command == "GET_RATE": answer="OK RATE 100"
            else: answer="OK STATUS READY RATE=100"
            self.input.append((answer+"\n").encode())
            return len(raw)
        def flush(self): pass
        def readline(self): return self.input.pop(0) if self.input else b""
        def reset_input_buffer(self): self.input.clear()
        def close(self): self.is_open=False

    fake=types.ModuleType("serial")
    fake.Serial=FakeSerial
    fake.SerialException=type("SerialException",(Exception,),{})
    monkeypatch.setitem(sys.modules,"serial",fake)
    with TestClient(app) as client:
        device_service.disconnect()
        req={"transport":"usb","serial_port":"/dev/ttyACM0","baudrate":115200,"timeout_sec":0.2}
        assert client.post("/api/connect",json=req).json()["reply"] == "OK PONG"
        assert client.post("/api/probe",json=req).json()["reply"] == "OK PONG"
        assert client.post("/api/get-rate",json=req).json()["reply"] == "OK RATE 100"
        assert len(opened)==1
        assert client.post("/api/disconnect").json()["success"] is True
        assert opened[0].is_open is False
        assert client.post("/api/ping",json=req).status_code==409
