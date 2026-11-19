"""Receive IMU and S.BUS telemetry and forward it to WebGUI clients."""

from __future__ import annotations

import asyncio
import re
from contextlib import asynccontextmanager
from ipaddress import IPv4Address

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from hikorobo_gcs.server.imu_raw_parser import parse_imu_raw_reply

IMU_STREAM_PORT = 5006


ATTITUDE_FIELDS = ("roll_deg", "pitch_deg", "yaw_deg")
RELATIVE_FIELDS = ("relative_roll_deg", "relative_pitch_deg", "relative_yaw_deg")
PACKET_FIELD_COUNTS = {"HK1": 10, "HK2": 16, "HK3": 19, "HK4": 23}
PHYSICAL_FIELDS = ("ax_mps2", "ay_mps2", "az_mps2", "gx_dps", "gy_dps", "gz_dps")


def parse_imu_packet(packet: bytes) -> dict[str, int | float | str]:
    if len(packet) > 271:
        raise ValueError("IMU packet too long")
    version, kind, boot_id, *fields = packet.decode("ascii").split()
    if version not in PACKET_FIELD_COUNTS or kind != "IMU" or not re.fullmatch(r"[0-9a-f]{8}", boot_id):
        raise ValueError("invalid IMU header")
    if len(fields) != PACKET_FIELD_COUNTS[version]:
        raise ValueError("invalid IMU field count")
    sample = parse_imu_raw_reply("OK IMU_RAW " + " ".join(fields[:10]))
    if sample["t_us"] > 2**53 - 1:
        raise ValueError("timestamp outside browser integer range")

    if version == "HK4":
        reference_seq = fields[19]
        if not re.fullmatch(r"[0-9]{1,10}", reference_seq) or not 1 <= int(reference_seq) <= sample["seq"]:
            raise ValueError("invalid attitude reference sequence")
        sample["reference_seq"] = int(reference_seq)

    physical = {}
    for key, value in zip(PHYSICAL_FIELDS + ATTITUDE_FIELDS + RELATIVE_FIELDS, fields[10:19] + fields[20:]):
        if not re.fullmatch(r"-?[0-9]{1,3}\.[0-9]{6}", value):
            raise ValueError(f"invalid IMU field {key}")
        physical[key] = float(value)
    for key, limit in zip(ATTITUDE_FIELDS + RELATIVE_FIELDS, (180, 90, 180) * 2):
        if abs(physical.get(key, 0)) > limit:
            raise ValueError(f"{key} outside attitude range")
    return {"boot_id": boot_id, **sample, **physical}


def parse_sbus_packet(packet: bytes) -> dict:
    if len(packet) > 64:
        raise ValueError("S.BUS packet too long")
    version, kind, lost, failsafe, *channels = packet.decode("ascii").split()
    if (version, kind) != ("HK1", "SBUS") or lost not in ("0", "1") or failsafe not in ("0", "1"):
        raise ValueError("invalid S.BUS header or flags")
    if len(channels) != 8 or any(not re.fullmatch(r"[0-9]{1,4}", ch) for ch in channels):
        raise ValueError("invalid S.BUS channels")
    values = list(map(int, channels))
    if any(value > 2047 for value in values):
        raise ValueError("S.BUS channel outside 11-bit range")
    return {"channels": values, "lost": lost == "1", "failsafe": failsafe == "1"}


class ImuReceiver(asyncio.DatagramProtocol):
    def __init__(self) -> None:
        self.subscribers: dict[asyncio.Queue, tuple[str, str]] = {}

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            kind = "SBUS" if data.startswith(b"HK1 SBUS ") else "IMU"
            sample = parse_sbus_packet(data) if kind == "SBUS" else parse_imu_packet(data)
        except ValueError:
            return

        for queue, destination in self.subscribers.items():
            if (addr[0], kind) != destination:
                continue
            if queue.full():
                queue.get_nowait()
            queue.put_nowait(sample)


@asynccontextmanager
async def imu_lifespan(app: FastAPI):
    receiver = ImuReceiver()
    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
        lambda: receiver, local_addr=("0.0.0.0", IMU_STREAM_PORT)
    )
    app.state.imu_receiver = receiver
    try:
        yield
    finally:
        transport.close()


async def stream_imu(websocket: WebSocket, host: str) -> None:
    await stream_telemetry(websocket, host, "IMU", queue_size=64)


async def stream_sbus(websocket: WebSocket, host: str) -> None:
    await stream_telemetry(websocket, host, "SBUS", queue_size=1)


async def stream_telemetry(websocket: WebSocket, host: str, kind: str, queue_size: int) -> None:
    try:
        source = str(IPv4Address(host))
    except ValueError:
        await websocket.close(code=1008)
        return

    receiver = websocket.app.state.imu_receiver
    queue: asyncio.Queue = asyncio.Queue(maxsize=queue_size)
    receiver.subscribers[queue] = (source, kind)
    try:
        await websocket.accept()
        while True:
            # Check for disconnects even when the XIAO is off or out of range.
            try:
                message = await asyncio.wait_for(websocket.receive(), timeout=0.05)
                if message["type"] == "websocket.disconnect":
                    break
            except asyncio.TimeoutError:
                pass

            samples = []
            while not queue.empty():
                samples.append(queue.get_nowait())
            if samples:
                await websocket.send_json({"samples": samples})
    except (WebSocketDisconnect, OSError):
        pass
    finally:
        del receiver.subscribers[queue]
