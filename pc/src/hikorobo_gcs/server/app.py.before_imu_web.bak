"""HikoRobo GCS command GUI, connection monitoring and UDP RTT benchmarking.

No IMU/SBUS streaming or telemetry is started by this server.
"""
from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from hikorobo_gcs.server.udp_commands import (
    DiscoverRequest,
    QualityRequest,
    RateRequest,
    TargetRequest,
    device_service,
    execute_command,
)

app = FastAPI(title="HikoRobo GCS", version="0.4.0")
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


@app.get("/api/serial-ports", tags=["Connection"])
def serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise HTTPException(status_code=503, detail="pyserialが未インストールです") from exc
    return {"ports": [{"device": p.device, "description": p.description} for p in list_ports.comports()]}


@app.post("/api/connect", tags=["Connection"])
def connect(request: TargetRequest):
    return device_service.connect(request)


@app.post("/api/disconnect", tags=["Connection"])
def disconnect():
    return device_service.disconnect()


@app.post("/api/discover", tags=["Connection"])
def discover(request: DiscoverRequest):
    return device_service.discover(request)


@app.post("/api/probe", tags=["Connection"])
def probe(request: TargetRequest):
    return execute_command(request, "PING", expected="OK PONG")


@app.post("/api/quality", tags=["Quality"])
def quality(request: QualityRequest):
    return device_service.quality(request)


@app.post("/api/ping", tags=["XIAO commands"])
def ping_xiao(request: TargetRequest):
    return execute_command(request, "PING", expected="OK PONG")


@app.post("/api/get-status", tags=["XIAO commands"])
def get_status(request: TargetRequest):
    return execute_command(request, "GET_STATUS", expected="OK STATUS ")


@app.post("/api/get-wifi", tags=["XIAO commands"])
def get_wifi(request: TargetRequest):
    return execute_command(request, "GET_WIFI", expected="OK WIFI ")


@app.post("/api/get-rate", tags=["XIAO commands"])
def get_rate(request: TargetRequest):
    return execute_command(request, "GET_RATE", expected="OK RATE ")


@app.post("/api/set-rate", tags=["XIAO commands"])
def set_rate(request: RateRequest):
    return execute_command(request, f"SET_RATE {request.rate}", expected=f"OK RATE {request.rate}")
