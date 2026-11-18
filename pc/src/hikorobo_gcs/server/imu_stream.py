"""Receive versioned IMU telemetry and fan it out to local WebGUI clients."""

from __future__ import annotations

import asyncio
import re
from contextlib import asynccontextmanager
from ipaddress import IPv4Address

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from hikorobo_gcs.server.imu_raw_parser import parse_imu_raw_reply

IMU_STREAM_PORT = 5006


def parse_imu_packet(packet: bytes) -> dict[str, int | str]:
    if len(packet) > 127:
        raise ValueError("IMU packet too long")
    version, kind, boot_id, fields = packet.decode("ascii").split(maxsplit=3)
    if (version, kind) != ("HK1", "IMU") or not re.fullmatch(r"[0-9a-f]{8}", boot_id):
        raise ValueError("invalid IMU header")
    sample = parse_imu_raw_reply(f"OK IMU_RAW {fields}")
    if sample["t_us"] > 2**53 - 1:
        raise ValueError("timestamp outside browser integer range")
    return {"boot_id": boot_id, **sample}


class ImuReceiver(asyncio.DatagramProtocol):
    def __init__(self) -> None:
        self.subscribers: dict[asyncio.Queue, str] = {}

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            sample = parse_imu_packet(data)
        except ValueError:
            return

        for queue, source in self.subscribers.items():
            if addr[0] != source:
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
    try:
        source = str(IPv4Address(host))
    except ValueError:
        await websocket.close(code=1008)
        return

    receiver = websocket.app.state.imu_receiver
    queue: asyncio.Queue = asyncio.Queue(maxsize=64)
    receiver.subscribers[queue] = source
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
