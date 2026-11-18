"""Strictly parse the textual LSM6DS3TR-C latest-sample response."""
from __future__ import annotations


def parse_imu_raw_reply(reply: str) -> dict[str, int]:
    # OK IMU_RAW seq t_us ax ay az gx gy gz temp 0xHH
    parts = reply.split()
    if len(parts) != 12 or parts[:2] != ["OK", "IMU_RAW"]:
        raise ValueError("expected 'OK IMU_RAW seq t_us ax ay az gx gy gz temp status'")
    labels = ("seq", "t_us", "ax", "ay", "az", "gx", "gy", "gz", "temp")
    try:
        # int(..., 10) also supports negative values in the signed fields.
        values = {key: int(value, 10) for key, value in zip(labels, parts[2:11])}
        if not parts[11].startswith("0x"):
            raise ValueError("status is not hexadecimal")
        values["status"] = int(parts[11], 16)
    except (TypeError, ValueError) as exc:
        raise ValueError("invalid integer in IMU response") from exc
    if values["seq"] < 1 or values["seq"] > 0xFFFFFFFF:
        raise ValueError("invalid sequence number")
    if values["t_us"] < 0:
        raise ValueError("invalid timestamp")
    for key in labels[2:]:
        if not -32768 <= values[key] <= 32767:
            raise ValueError(f"{key} outside int16 range")
    if not 0 <= values["status"] <= 255:
        raise ValueError("invalid status register")
    return values
