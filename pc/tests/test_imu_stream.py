import socket

import pytest

from fastapi.testclient import TestClient

from hikorobo_gcs.server import imu_stream
from hikorobo_gcs.server.app import app


@pytest.mark.parametrize("version", [b"HK1", b"HK2"])
def test_imu_udp_to_websocket(monkeypatch, version):
    raw = b" IMU 14d1c6b9 42 4100000 -12 5 16384 3 -2 11 0 0x03"
    physical = {}
    converted = b""
    if version == b"HK2":
        converted = b" -0.007178 0.002991 9.801001 0.026250 -0.017500 0.096250"
        physical = {
            "ax_mps2": -0.007178, "ay_mps2": 0.002991, "az_mps2": 9.801001,
            "gx_dps": 0.026250, "gy_dps": -0.017500, "gz_dps": 0.096250,
        }
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
