"""HikoRobo GCS command GUI, connection monitoring and IMU telemetry."""
from __future__ import annotations

import re
from pathlib import Path
from typing import Literal

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import Field

from hikorobo_gcs.server.imu_raw_parser import parse_imu_raw_reply
from hikorobo_gcs.server.imu_stream import imu_lifespan, stream_imu, stream_sbus
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
app.add_api_websocket_route("/ws/sbus", stream_sbus)
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


class ImuBiasStartRequest(TargetRequest):
    axis: Literal["+X", "-X", "+Y", "-Y", "+Z", "-Z"]


def imu_bias_command(request: TargetRequest, command: str):
    result = execute_command(request, command, expected="OK IMU_BIAS ")
    match = re.fullmatch(r"OK IMU_BIAS (IDLE|RUNNING|DONE|UNSTABLE|TIMEOUT) ([0-9]{1,2}) ([01])",
                         result["reply"])
    if not match or int(match[2]) > 30:
        raise HTTPException(status_code=502, detail="IMU補正状態の応答形式が不正です")
    return {"state": match[1], "count": int(match[2]), "applied": match[3] == "1"}


@app.post("/api/imu-bias/status", tags=["IMU"])
def get_imu_bias_status(request: TargetRequest):
    return imu_bias_command(request, "GET_IMU_BIAS")


@app.post("/api/imu-bias/start", tags=["IMU"])
def start_imu_bias(request: ImuBiasStartRequest):
    return imu_bias_command(request, f"IMU_BIAS START {request.axis}")


@app.post("/api/imu-bias/reset", tags=["IMU"])
def reset_imu_bias(request: TargetRequest):
    return imu_bias_command(request, "IMU_BIAS RESET")


class ImuAttitudeRequest(TargetRequest):
    filter: Literal["COMPLEMENTARY", "MAHONY"]
    tau_ms: int = Field(ge=50, le=30000)
    kp_milli: int = Field(ge=0, le=10000)
    ki_milli: int = Field(ge=0, le=2000)


def imu_attitude_command(request: TargetRequest, command: str):
    result = execute_command(request, command, expected="OK IMU_ATT ")
    match = re.fullmatch(r"OK IMU_ATT (COMPLEMENTARY|MAHONY) (\d{1,5}) (\d{1,5}) (\d{1,5})",
                         result["reply"])
    if not match:
        raise HTTPException(status_code=502, detail="姿勢フィルタの応答形式が不正です")
    tau_ms, kp_milli, ki_milli = map(int, match.groups()[1:])
    if not (50 <= tau_ms <= 30000 and 0 <= kp_milli <= 10000 and 0 <= ki_milli <= 2000):
        raise HTTPException(status_code=502, detail="姿勢フィルタの応答値が範囲外です")
    return {"filter": match[1], "tau_ms": tau_ms, "kp_milli": kp_milli, "ki_milli": ki_milli}


@app.post("/api/imu-attitude/get", tags=["IMU"])
def get_imu_attitude(request: TargetRequest):
    return imu_attitude_command(request, "GET_IMU_ATT")


@app.post("/api/imu-attitude/set", tags=["IMU"])
def set_imu_attitude(request: ImuAttitudeRequest):
    return imu_attitude_command(
        request, f"SET_IMU_ATT {request.filter} {request.tau_ms} {request.kp_milli} {request.ki_milli}"
    )


@app.post("/api/imu-attitude/reference/{action}", tags=["IMU"])
def update_attitude_reference(action: Literal["set", "clear"], request: TargetRequest):
    result = execute_command(request, f"IMU_REF {action.upper()}", expected="OK IMU_REF ")
    match = re.fullmatch(r"OK IMU_REF ([0-9]{1,10})", result["reply"])
    if not match:
        raise HTTPException(status_code=502, detail="姿勢基準の応答形式が不正です")
    seq = int(match[1])
    if (action == "clear" and seq != 0) or (action == "set" and not 1 <= seq <= 0xFFFFFFFF):
        raise HTTPException(status_code=502, detail="姿勢基準の連番が不正です")
    return {"reference_seq": seq}


class SbusSettingsRequest(TargetRequest):
    values: list[int] | None = Field(default=None, min_length=4, max_length=5)


@app.post("/api/sbus/{section}", tags=["S.BUS"])
def sbus_settings(section: str, request: SbusSettingsRequest):
    if request.transport != "udp":
        raise HTTPException(422, "S.BUS 設定には UDP を選択してください")
    values = request.values
    if section == "save" and values is None:
        execute_command(request, "SBUS SAVE", expected="OK SBUS SAVE")
        return {"saved": True}
    if section == "apply-defaults" and values is None:
        execute_command(request, "SBUS APPLY_DEFAULTS", expected="OK SBUS APPLY_DEFAULTS")
        return {"applied": True}
    if section == "mode":
        suffix, prefix, count = "MODE", "OK SBUS MODE ", 5
    elif section == "axes" and values is None:
        suffix, prefix, count = "AXES", "OK SBUS AXES ", 3
    elif re.fullmatch(r"[1-8]", section):
        suffix, prefix, count = f"CH {section}", f"OK SBUS CH {section} ", 4
    else:
        raise HTTPException(404, "S.BUS 設定項目が不正です")
    if values is not None and (len(values) != count or any(abs(v) > 18000 for v in values)):
        raise HTTPException(422, "S.BUS 設定値が不正です")
    command = f"SBUS GET_{suffix}" if values is None else f"SBUS SET_{suffix} " + " ".join(map(str, values))
    result = execute_command(request, command, expected=prefix)
    fields = result["reply"][len(prefix):].split()
    if len(fields) != count or any(not re.fullmatch(r"-?[0-9]{1,5}", field) for field in fields):
        raise HTTPException(502, "S.BUS 設定応答の形式が不正です")
    return {"values": list(map(int, fields))}
