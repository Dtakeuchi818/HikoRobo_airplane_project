"""HikoRobo GCS: Web GUI and HTTP-to-XIAO UDP command API.

Uses the existing hikorobo_gcs.udp_cli.exchange() transport.
No START_STREAM/STOP_STREAM or telemetry receiver is invoked.
"""
from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from hikorobo_gcs.server.udp_commands import (
    RateRequest,
    TargetRequest,
    execute_command,
)

app = FastAPI(title="HikoRobo GCS", version="0.3.0")

WEB_DIR = Path(__file__).resolve().parent.parent / "web"
app.mount("/static", StaticFiles(directory=WEB_DIR / "static"), name="static")


@app.get("/", include_in_schema=False)
def index():
    return FileResponse(WEB_DIR / "index.html", headers={"Cache-Control": "no-store"})


@app.get("/api/hello")
def hello():
    return {"message": "Hello, HikoRobo!"}


@app.get("/api/health")
def health():
    return {"status": "ok"}


@app.post("/api/ping", tags=["XIAO UDP commands"])
def ping_xiao(request: TargetRequest):
    return execute_command(request, "PING", expected="OK PONG")


@app.post("/api/get-status", tags=["XIAO UDP commands"])
def get_status(request: TargetRequest):
    return execute_command(request, "GET_STATUS", expected="OK STATUS ")


@app.post("/api/get-wifi", tags=["XIAO UDP commands"])
def get_wifi(request: TargetRequest):
    return execute_command(request, "GET_WIFI", expected="OK WIFI ")


@app.post("/api/get-rate", tags=["XIAO UDP commands"])
def get_rate(request: TargetRequest):
    return execute_command(request, "GET_RATE", expected="OK RATE ")


@app.post("/api/set-rate", tags=["XIAO UDP commands"])
def set_rate(request: RateRequest):
    command = f"SET_RATE {request.rate}"
    return execute_command(request, command, expected=f"OK RATE {request.rate}")