# HikoRobo UDP protocol v1 (development-only)

## Network
- Ubuntu 24.04: NetworkManager 2.4 GHz WPA2 hotspot, SSID `HikoRobo_Lab`.
- XIAO ESP32S3 Plus: station mode, DHCP, UDP server `0.0.0.0:5005`.
- PC: UDP client using an ephemeral local UDP port; replies go to request sender's IP/port.
- XIAO discovery: send `PING\n` to the hotspot's subnet broadcast address on UDP 5005 (typically `10.42.0.255`); use the source IP of `OK PONG`.
- **No internet connection is required.** IP addresses are determined by DHCP, not hard-coded.

## Datagram format
- ONE command per datagram. Printable ASCII only; max **95 characters** excluding optional `\r` or `\n` at the end.
- The client appends `\n` for consistency with the USB version; a datagram without LF also works.
- The server replies with one `OK ...\n` or `ERR ...\n` datagram.
- Commands are case sensitive. Multiple commands in one datagram are rejected (`ERR BAD_CHARACTER`).

| Request | Example reply | Notes |
|---|---|---|
| `PING` | `OK PONG` | also used in discovery |
| `GET_WIFI` | `OK WIFI CONNECTED IP=10.42.0.23` | link status; on USB can say `OK WIFI DISCONNECTED` |
| `GET_STATUS` | `OK STATUS READY RATE=100` | READY is a test string, not actual sensor readiness |
| `GET_RATE` | `OK RATE 100` | test variable |
| `SET_RATE 250` | `OK RATE 250` | 1..500, no sensor changed |
| `HELP` | `OK COMMANDS ...` | command list |
| `SET_RATE 999` | `ERR RATE_RANGE` | invalid numeric range |
| `UNKNOWN` | `ERR UNKNOWN_COMMAND` | unrecognized command |

## Limitations and security
- UDP is connectionless. Packet delivery, order and uniqueness are not guaranteed. The PC sends requests **sequentially** and matches only sender IP/port; protocol v1 has no sequence/request IDs.
- No application-level authentication, encryption, retransmissions, or safety controls. WPA2 protects the hotspot, but anyone connected to the LAN can send requests. **Do not connect this test protocol directly to flight-critical actuators.**
- `SET_RATE` changes an in-RAM test variable. It is shared between USB and UDP; reboot restores 100.
- The Wi-Fi password is saved in local `sdkconfig` and embedded in the firmware binary. Never commit or share either.
- `GET_WIFI` status provider depends on Wi-Fi being initialized; after boot it returns disconnected until IP acquisition.
- Wi-Fi disconnect/IP change increments a network generation number, triggering UDP socket recreation; server checks it on a receive timeout (0.5 s).
- RTT from `PING` measures application round trip, not one-way radio latency or the flight-control loop frequency.
