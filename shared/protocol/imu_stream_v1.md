# IMU telemetry over UDP

The XIAO sends one ASCII datagram per acquired sample to the configured ground PC IPv4 address on UDP port 5006. Existing commands use UDP port 5005.

```text
HK1 IMU 14d1c6b9 42 4100000 -12 5 16384 3 -2 11 0 0x03
```

Fields in order are version, stream type, boot ID, sequence, acquisition timestamp, acceleration XYZ, angular velocity XYZ, temperature, and status.

- `boot_id` is an eight-digit lowercase hexadecimal ID generated at stream task startup.
- `seq` identifies a sample in the IMU stream. It starts at 1 and advances with each successful read.
- `t_us` is the XIAO's monotonic acquisition timestamp in microseconds.
- The seven sensor values are signed 16-bit decimal counts. Status is an unsigned byte in hexadecimal.
- UDP delivery and ordering are best effort. The receiver can detect gaps using the sequence and timestamp.

The GCS subscribes per device IPv4 via `/ws/imu?host=<XIAO IPv4>`. WebSocket messages contain a `samples` array. Each item contains `boot_id`, `seq`, `t_us`, `ax`, `ay`, `az`, `gx`, `gy`, `gz`, `temp` and `status`.
