# USB serial command protocol v1

USB Serial/JTAG of ESP32-S3. The transport is byte-oriented; the protocol is line-oriented.

- Encoding: 7-bit printable ASCII, case-sensitive.
- Every request and response ends in `\n` (LF); incoming `\r` before LF is ignored.
- Maximum request length: 95 characters (excluding LF).
- Exactly one response to each non-empty request line.
- Response starts with `OK ` or `ERR `.
- Unsolicited telemetry is **not** part of this initial version.

| Request | Example response | Meaning |
| --- | --- | --- |
| `PING` | `OK PONG` | Link test |
| `GET_STATUS` | `OK STATUS READY RATE=100` | Status and rate |
| `GET_RATE` | `OK RATE 100` | Read dummy rate |
| `SET_RATE 250` | `OK RATE 250` | Set rate, range 1–500 |
| `HELP` | `OK COMMANDS PING GET_STATUS GET_RATE SET_RATE HELP` | Supported commands |
| unknown | `ERR UNKNOWN_COMMAND` | Unknown command |
| invalid set value | `ERR BAD_ARGUMENT` | Syntax error |
| outside range | `ERR RATE_RANGE` | Range error |

`RATE` is a RAM-only dummy parameter, not an actual IMU sampling frequency. It resets to 100 on reboot. This minimal protocol has no authentication, CRC, command IDs, or concurrency guarantees and is only for USB bench testing. For later Wi-Fi use, extend it with explicit IDs, acknowledgements, versioning, bounded retries, and permissions for safety-critical changes.
