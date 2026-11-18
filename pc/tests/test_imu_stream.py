import socket

import pytest

from fastapi.testclient import TestClient

from hikorobo_gcs.server import imu_stream
from hikorobo_gcs.server.app import app


@pytest.mark.parametrize("version", [b"HK1", b"HK2", b"HK3", b"HK4"])
def test_imu_udp_to_websocket(monkeypatch, version):
    raw = b" IMU 14d1c6b9 42 4100000 -12 5 16384 3 -2 11 0 0x03"
    physical = {}
    converted = b""
    if version != b"HK1":
        converted = b" -0.007178 0.002991 9.801001 0.026250 -0.017500 0.096250"
        physical = {
            "ax_mps2": -0.007178, "ay_mps2": 0.002991, "az_mps2": 9.801001,
            "gx_dps": 0.026250, "gy_dps": -0.017500, "gz_dps": 0.096250,
        }
    if version in (b"HK3", b"HK4"):
        converted += b" -12.500000 2.125000 179.000000"
        physical.update(roll_deg=-12.5, pitch_deg=2.125, yaw_deg=179.0)
    if version == b"HK4":
        converted += b" 40 0.000000 -1.000000 2.000000"
        physical.update(reference_seq=40, relative_roll_deg=0.0, relative_pitch_deg=-1.0, relative_yaw_deg=2.0)
    monkeypatch.setattr(imu_stream, "IMU_STREAM_PORT", 0)
    with TestClient(app) as client:
        port = app.state.imu_receiver.transport.get_extra_info("sockname")[1]
        with client.websocket_connect("/ws/imu?host=127.0.0.1") as websocket:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                destination = ("127.0.0.1", port)
                sender.sendto(b"HK1 SBUS unsupported", destination)
                sender.sendto(version + raw + converted, destination)
            assert websocket.receive_json() == {
                "samples": [{
                    "boot_id": "14d1c6b9", "seq": 42, "t_us": 4100000,
                    "ax": -12, "ay": 5, "az": 16384,
                    "gx": 3, "gy": -2, "gz": 11, "temp": 0, "status": 3, **physical,
                }]
            }


def test_imu_bias_commands(monkeypatch):
    from hikorobo_gcs.server import app as app_module

    monkeypatch.setattr(imu_stream, "IMU_STREAM_PORT", 0)
    commands = []

    def execute(request, command, *, expected):
        commands.append(command)
        assert str(request.host) == "127.0.0.1"
        assert expected == "OK IMU_BIAS "
        replies = {
            "IMU_BIAS START -Z": "OK IMU_BIAS RUNNING 0 0",
            "GET_IMU_BIAS": "OK IMU_BIAS DONE 30 1",
            "IMU_BIAS RESET": "OK IMU_BIAS IDLE 0 0",
        }
        return {"reply": replies[command]}

    monkeypatch.setattr(app_module, "execute_command", execute)
    with TestClient(app) as client:
        target = {"host": "127.0.0.1"}
        assert client.post("/api/imu-bias/start", json={**target, "axis": "+W"}).status_code == 422
        assert client.post("/api/imu-bias/start", json={**target, "axis": "-Z"}).json() == {
            "state": "RUNNING", "count": 0, "applied": False,
        }
        assert client.post("/api/imu-bias/status", json=target).json() == {
            "state": "DONE", "count": 30, "applied": True,
        }
        assert client.post("/api/imu-bias/reset", json=target).json() == {
            "state": "IDLE", "count": 0, "applied": False,
        }
    assert commands == ["IMU_BIAS START -Z", "GET_IMU_BIAS", "IMU_BIAS RESET"]


def test_imu_attitude_commands(monkeypatch):
    from hikorobo_gcs.server import app as app_module

    monkeypatch.setattr(imu_stream, "IMU_STREAM_PORT", 0)
    commands = []

    def execute(request, command, *, expected):
        commands.append(command)
        assert str(request.host) == "127.0.0.1"
        assert expected == "OK IMU_ATT "
        replies = {
            "GET_IMU_ATT": "OK IMU_ATT COMPLEMENTARY 500 1000 50",
            "SET_IMU_ATT MAHONY 750 2200 80": "OK IMU_ATT MAHONY 750 2200 80",
        }
        return {"reply": replies[command]}

    monkeypatch.setattr(app_module, "execute_command", execute)
    with TestClient(app) as client:
        target = {"host": "127.0.0.1"}
        assert client.post("/api/imu-attitude/get", json=target).json() == {
            "filter": "COMPLEMENTARY", "tau_ms": 500, "kp_milli": 1000, "ki_milli": 50,
        }
        settings = {"filter": "MAHONY", "tau_ms": 750, "kp_milli": 2200, "ki_milli": 80}
        assert client.post("/api/imu-attitude/set", json={**target, **settings, "tau_ms": 0}).status_code == 422
        assert client.post("/api/imu-attitude/set", json={**target, **settings}).json() == settings
    assert commands == ["GET_IMU_ATT", "SET_IMU_ATT MAHONY 750 2200 80"]


def test_imu_attitude_reference_commands(monkeypatch):
    from hikorobo_gcs.server import app as app_module

    monkeypatch.setattr(imu_stream, "IMU_STREAM_PORT", 0)
    commands = []

    def execute(request, command, *, expected):
        commands.append(command)
        assert str(request.host) == "127.0.0.1"
        assert expected == "OK IMU_REF "
        return {"reply": {"IMU_REF SET": "OK IMU_REF 40", "IMU_REF CLEAR": "OK IMU_REF 0"}[command]}

    monkeypatch.setattr(app_module, "execute_command", execute)
    with TestClient(app) as client:
        target = {"host": "127.0.0.1"}
        assert client.post("/api/imu-attitude/reference/set", json=target).json() == {"reference_seq": 40}
        assert client.post("/api/imu-attitude/reference/clear", json=target).json() == {"reference_seq": 0}
    assert commands == ["IMU_REF SET", "IMU_REF CLEAR"]
