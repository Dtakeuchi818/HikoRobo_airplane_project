"""HTTP models, shared device operations and quality measurement.

UDP is one-shot per request. USB serial is kept open until disconnect.
A single lock prevents command replies from being mixed with probes/benchmarks.
"""
from __future__ import annotations

import math
import socket
import statistics
import sys
import threading
import time
from datetime import datetime, timezone
from ipaddress import IPv4Address
from typing import Literal

from fastapi import HTTPException
from pydantic import BaseModel, Field, field_validator

from hikorobo_gcs.udp_cli import discover as udp_discover
from hikorobo_gcs.udp_cli import exchange as udp_exchange
from hikorobo_gcs.serial_cli import exchange as serial_exchange


class TargetRequest(BaseModel):
    transport: Literal["udp", "usb"] = "udp"
    host: IPv4Address = IPv4Address("10.42.0.162")
    port: int = Field(default=5005, ge=1, le=65535)
    timeout_sec: float = Field(default=1.0, ge=0.1, le=3.0, allow_inf_nan=False)
    serial_port: str = Field(default="/dev/ttyACM0", min_length=1, max_length=128)
    baudrate: int = Field(default=115200, ge=1200, le=3000000)

    @field_validator("serial_port")
    @classmethod
    def check_serial_port(cls, value: str) -> str:
        # serial.Serial opens only OS devices, not pyserial URL handlers.
        if not value or "\x00" in value or "\n" in value or "\r" in value or "://" in value:
            raise ValueError("シリアルポート名が不正です")
        return value


class RateRequest(TargetRequest):
    rate: int = Field(ge=1, le=500)


class DiscoverRequest(BaseModel):
    broadcast: IPv4Address = IPv4Address("10.42.0.255")
    port: int = Field(default=5005, ge=1, le=65535)
    timeout_sec: float = Field(default=1.0, ge=0.1, le=3.0, allow_inf_nan=False)


class QualityRequest(TargetRequest):
    count: int = Field(default=30, ge=5, le=100)
    interval_ms: int = Field(default=200, ge=50, le=1000)


class DeviceService:
    """In-process, single-device command dispatcher; run Uvicorn with one worker."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._serial = None
        self._serial_config: tuple[str, int] | None = None

    def _acquire(self):
        if not self._lock.acquire(blocking=False):
            raise HTTPException(status_code=409, detail="ほかの通信処理を実行中です。完了後に再試行してください")

    def _close_serial(self):
        device = self._serial
        self._serial = None
        self._serial_config = None
        if device is not None:
            try:
                device.close()
            except Exception:
                pass

    def _open_serial(self, request: TargetRequest):
        try:
            import serial
        except ImportError as exc:
            raise HTTPException(status_code=503, detail="pyserialが未インストールです") from exc
        desired = (request.serial_port, request.baudrate)
        if self._serial is not None and self._serial_config == desired and self._serial.is_open:
            return
        self._close_serial()
        kwargs = {
            "port": request.serial_port,
            "baudrate": request.baudrate,
            "timeout": min(0.2, request.timeout_sec),
            "write_timeout": request.timeout_sec,
        }
        if sys.platform.startswith("linux"):
            kwargs["exclusive"] = True
        try:
            device = serial.Serial(**kwargs)
            # Briefly allow startup / stray serial output, as in serial_cli.py.
            time.sleep(0.2)
            device.reset_input_buffer()
        except (OSError, serial.SerialException) as exc:
            if "device" in locals():
                try:
                    device.close()
                except Exception:
                    pass
            raise HTTPException(status_code=503, detail=f"USBポートを開けません: {exc}") from exc
        self._serial = device
        self._serial_config = desired

    def _exchange(self, request: TargetRequest, command: str):
        if request.transport == "udp":
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                return udp_exchange(sock, (str(request.host), request.port), command, request.timeout_sec)
        if self._serial is None or not self._serial.is_open:
            raise HTTPException(status_code=409, detail="USBが未接続です。「接続確認」を押してください")
        if self._serial_config != (request.serial_port, request.baudrate):
            raise HTTPException(status_code=409, detail="USB接続設定が変わりました。再接続してください")
        start = time.perf_counter()
        reply = serial_exchange(self._serial, command, request.timeout_sec)
        return reply, (time.perf_counter() - start) * 1000

    @staticmethod
    def _check_reply(reply: str, expected: str):
        if reply.startswith("ERR"):
            raise HTTPException(status_code=502, detail=f"XIAOのエラー応答: {reply}")
        if reply != expected and not (expected.endswith(" ") and reply.startswith(expected)):
            raise HTTPException(status_code=502, detail=f"予期しない応答: {reply}")

    @staticmethod
    def _map_exception(exc: Exception):
        if isinstance(exc, HTTPException):
            raise exc
        if isinstance(exc, (TimeoutError, socket.timeout)):
            raise HTTPException(status_code=504, detail=f"XIAOの応答がありません: {exc}") from exc
        # SerialException is a subclass of OSError in some pyserial versions, but not all.
        if isinstance(exc, (OSError, ValueError)) or exc.__class__.__name__ in {"SerialException", "SerialTimeoutException"}:
            raise HTTPException(status_code=502, detail=f"通信エラー: {exc}") from exc
        raise exc

    def execute(self, request: TargetRequest, command: str, expected: str):
        self._acquire()
        try:
            reply, rtt = self._exchange(request, command)
            self._check_reply(reply, expected)
            return {"success": True, "transport": request.transport, "command": command,
                    "reply": reply, "rtt_ms": round(rtt, 3)}
        except Exception as exc:
            self._map_exception(exc)
        finally:
            self._lock.release()

    def connect(self, request: TargetRequest):
        self._acquire()
        opened_new_serial = False
        try:
            if request.transport == "usb":
                old_config = self._serial_config
                self._open_serial(request)
                opened_new_serial = old_config != self._serial_config
            else:
                self._close_serial()
            reply, rtt = self._exchange(request, "PING")
            self._check_reply(reply, "OK PONG")
            return {"success": True, "transport": request.transport, "reply": reply,
                    "rtt_ms": round(rtt, 3)}
        except Exception as exc:
            if request.transport == "usb" and opened_new_serial:
                self._close_serial()
            self._map_exception(exc)
        finally:
            self._lock.release()

    def disconnect(self):
        self._acquire()
        try:
            self._close_serial()
            return {"success": True}
        finally:
            self._lock.release()

    def discover(self, request: DiscoverRequest):
        self._acquire()
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                peer = udp_discover(sock, str(request.broadcast), request.port, request.timeout_sec)
            return {"success": True, "host": peer[0], "port": peer[1]}
        except Exception as exc:
            self._map_exception(exc)
        finally:
            self._lock.release()

    def quality(self, request: QualityRequest):
        if request.transport != "udp":
            raise HTTPException(status_code=422, detail="ネットワーク品質測定はUDP専用です")
        self._acquire()
        try:
            samples = []
            successful = []
            for i in range(request.count):
                stamp = datetime.now(timezone.utc).isoformat()
                try:
                    reply, rtt = self._exchange(request, "PING")
                    self._check_reply(reply, "OK PONG")
                    samples.append({"index": i + 1, "timestamp_utc": stamp,
                                    "success": True, "rtt_ms": round(rtt, 3), "reply": reply})
                    successful.append(rtt)
                except (OSError, TimeoutError, ValueError, HTTPException) as exc:
                    samples.append({"index": i + 1, "timestamp_utc": stamp,
                                    "success": False, "rtt_ms": None, "reply": str(exc)})
                if i + 1 < request.count:
                    time.sleep(request.interval_ms / 1000)
            n_success = len(successful)
            sorted_rtt = sorted(successful)
            p95 = sorted_rtt[math.ceil(n_success * 0.95) - 1] if n_success else None
            # Absolute RTT difference between successive *successful* probes.
            jitter = (statistics.mean(abs(b-a) for a, b in zip(successful, successful[1:]))
                      if n_success >= 2 else None)
            return {
                "transport": "udp", "count": request.count, "success_count": n_success,
                "failure_count": request.count - n_success,
                "unanswered_percent": round(100 * (request.count - n_success) / request.count, 2),
                "min_rtt_ms": round(min(successful), 3) if successful else None,
                "mean_rtt_ms": round(statistics.mean(successful), 3) if successful else None,
                "median_rtt_ms": round(statistics.median(successful), 3) if successful else None,
                "p95_rtt_ms": round(p95, 3) if p95 is not None else None,
                "max_rtt_ms": round(max(successful), 3) if successful else None,
                "rtt_variation_ms": round(jitter, 3) if jitter is not None else None,
                "samples": samples,
            }
        finally:
            self._lock.release()


device_service = DeviceService()


def execute_command(request: TargetRequest, command: str, *, expected: str):
    """Retain the function used by the prior /api/ping and commands routes."""
    return device_service.execute(request, command, expected)
