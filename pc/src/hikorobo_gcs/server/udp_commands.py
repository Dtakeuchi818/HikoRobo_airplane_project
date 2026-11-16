"""Validated HTTP request models and one-shot UDP command execution."""
from __future__ import annotations

import socket
from ipaddress import IPv4Address

from fastapi import HTTPException
from pydantic import BaseModel, Field

from hikorobo_gcs.udp_cli import exchange


class TargetRequest(BaseModel):
    host: IPv4Address
    port: int = Field(default=5005, ge=1, le=65535)
    timeout_sec: float = Field(default=1.0, gt=0, le=10)


class RateRequest(TargetRequest):
    # Actual command_protocol.c range is 1..500.
    rate: int = Field(ge=1, le=500)


def execute_command(request: TargetRequest, command: str, *, expected: str):
    """Send a single command; use a new UDP socket for each HTTP request.

    The existing exchange() checks that the reply came from the requested IP:port.
    This is intentionally synchronous: FastAPI runs a 'def' endpoint in a threadpool.
    """
    peer = (str(request.host), request.port)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            reply, rtt_ms = exchange(sock, peer, command, request.timeout_sec)
    except (TimeoutError, socket.timeout) as exc:
        raise HTTPException(
            status_code=504,
            detail=f"XIAOのUDP応答がありません: {exc}",
        ) from exc
    except OSError as exc:
        raise HTTPException(
            status_code=502,
            detail=f"UDP通信エラー: {exc}",
        ) from exc

    if reply.startswith("ERR"):
        raise HTTPException(
            status_code=502,
            detail=f"XIAOがエラーを返しました: {reply}",
        )
    if reply != expected and not (expected.endswith(" ") and reply.startswith(expected)):
        raise HTTPException(
            status_code=502,
            detail=f"予期しないXIAO応答: {reply}",
        )

    return {
        "success": True,
        "command": command,
        "reply": reply,
        "rtt_ms": round(rtt_ms, 2),
    }
