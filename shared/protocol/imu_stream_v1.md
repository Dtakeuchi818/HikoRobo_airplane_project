# IMU telemetry over UDP

The XIAO sends one ASCII datagram per acquired sample to the configured ground PC IPv4 address on UDP port 5006. Existing commands use UDP port 5005.

```text
HK2 IMU 14d1c6b9 42 4100000 -12 5 16384 3 -2 11 0 0x03 -0.007178 0.002991 9.801001 0.026250 -0.017500 0.096250
```

Fields in order are version, stream type, boot ID, sequence, acquisition timestamp, acceleration XYZ, angular velocity XYZ, temperature, status, converted acceleration XYZ, and converted angular velocity XYZ.

- `boot_id` is an eight-digit lowercase hexadecimal ID generated at stream task startup.
- `seq` identifies a sample in the IMU stream. It starts at 1 and advances with each successful read.
- `t_us` is the XIAO's monotonic acquisition timestamp in microseconds.
- The seven sensor values are signed 16-bit decimal counts. Status is an unsigned byte in hexadecimal.
- In `HK2`, the six converted values use decimal notation with six fractional digits. Acceleration uses m/s² and angular velocity uses °/s. The converted fields include any active stationary bias correction on the XIAO. The GCS also accepts `HK1`, which ends after status.
- UDP delivery and ordering are best effort. The receiver can detect gaps using the sequence and timestamp.

The GCS subscribes per device IPv4 via `/ws/imu?host=<XIAO IPv4>`. WebSocket messages contain a `samples` array. Each item contains `boot_id`, `seq`, `t_us`, `ax`, `ay`, `az`, `gx`, `gy`, `gz`, `temp` and `status`.

For `HK2`, each item also contains `ax_mps2`, `ay_mps2`, `az_mps2`, `gx_dps`, `gy_dps` and `gz_dps`.


Bias commands use the existing command channel (UDP port 5005 or USB). `IMU_BIAS START +Z` begins sampling with the specified axis pointing up. The supported arguments are `+X`, `-X`, `+Y`, `-Y`, `+Z` and `-Z`. `IMU_BIAS RESET` clears the correction and cancels any measurement. `GET_IMU_BIAS` queries the state. Replies have the form `OK IMU_BIAS <state> <count> <applied>`. States are `IDLE`, `RUNNING`, `DONE`, `UNSTABLE` and `TIMEOUT`, and `applied` is 0 or 1.
