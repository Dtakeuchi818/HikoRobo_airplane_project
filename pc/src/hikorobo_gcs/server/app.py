"""HikoRobo GCS command GUI, connection monitoring and IMU telemetry."""
from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from hikorobo_gcs.server.imu_raw_parser import parse_imu_raw_reply
from hikorobo_gcs.server.imu_stream import imu_lifespan, stream_imu
from hikorobo_gcs.server.udp_commands import (
    DiscoverRequest,
    QualityRequest,
    RateRequest,
    TargetRequest,
    device_service,
    execute_command,
)

app = FastAPI(title="HikoRobo GCS", version="0.4.0", lifespan=imu_lifespan)
app.add_api_websocket_route("/ws/imu", stream_imu)
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


# GET_IMU_RAW uses the existing UDP 5005 command request/reply path.


@app.post("/api/get-imu-raw", tags=["IMU"])
def get_imu_raw(request: TargetRequest):
    if request.transport != "udp":
        raise HTTPException(status_code=422, detail="IMU生値の初期版はUDP通信で使用してください")
    result = execute_command(request, "GET_IMU_RAW", expected="OK IMU_RAW ")
    try:
        result["sample"] = parse_imu_raw_reply(result["reply"])
    except ValueError as exc:
        raise HTTPException(status_code=502, detail=f"IMU応答の形式が不正です: {exc}") from exc
    return result
