import socket

from fastapi.testclient import TestClient

from hikorobo_gcs.server import imu_stream
from hikorobo_gcs.server.app import app


def test_imu_udp_to_websocket(monkeypatch):
    monkeypatch.setattr(imu_stream, "IMU_STREAM_PORT", 0)
    with TestClient(app) as client:
        port = app.state.imu_receiver.transport.get_extra_info("sockname")[1]
        with client.websocket_connect("/ws/imu?host=127.0.0.1") as websocket:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                destination = ("127.0.0.1", port)
                sender.sendto(b"HK1 SBUS unsupported", destination)
                sender.sendto(
                    b"HK1 IMU 14d1c6b9 42 4100000 -12 5 16384 3 -2 11 0 0x03",
                    destination,
                )
            assert websocket.receive_json() == {
                "samples": [{
                    "boot_id": "14d1c6b9", "seq": 42, "t_us": 4100000,
                    "ax": -12, "ay": 5, "az": 16384,
                    "gx": 3, "gy": -2, "gz": 11, "temp": 0, "status": 3,
                }]
            }
