# Implementation guide — Step 3 Wi-Fi AP + UDP

## End-to-end execution flow

1. Ubuntu `scripts/ap_start.sh wlp...` creates a NetworkManager hotspot with IPv4 shared and DHCP; e.g. PC IP 10.42.0.1.
2. The ESP32-S3 second-stage bootloader launches the ESP-IDF firmware from its factory partition.
3. `app_main()` calls `wifi_manager_start()`: initializes NVS, esp-netif, event loop, and Wi-Fi station; it starts Wi-Fi non-blockingly.
4. `app_main()` registers a Wi-Fi status callback with `command_protocol`; starts separate FreeRTOS tasks for USB command input and UDP server. It then returns.
5. ESP-IDF event handler calls `esp_wifi_connect()` on start or disconnect; when DHCP completes, `IP_EVENT_STA_GOT_IP` sets the READY bit.
6. The UDP task waits for READY, creates an IPv4 UDP socket, binds `0.0.0.0:5005`, and receives datagrams via `recvfrom()`.
7. UDP transport validates one printable-ASCII command per packet, calls `command_protocol_handle()`, and replies to the *same source address* using `sendto()`.
8. If the Wi-Fi disconnects or the IP changes, the manager increments the `network_epoch`, and the UDP task closes and recreates the socket.
9. `pc/.../udp_cli.py` discovers XIAO via broadcast `PING`, or targets the learned IP; its `exchange()` verifies the reply's source IP/port and measures RTT.

## Separation of modules

| Module | What it owns | Why separated |
|---|---|---|
| `wifi_manager` | Wi-Fi init, events, link status, DHCP IP, reconnection | UDP need not know ESP-IDF Wi-Fi event details |
| `udp_transport` | Socket lifecycle, datagram parsing, replies | Other transports can share commands |
| `usb_transport` | USB Serial/JTAG bytes and lines | Still provides wired diagnostics if Wi-Fi fails |
| `command_protocol` | PING, GET_WIFI, GET_RATE, SET_RATE, HELP, validation | No USB or Wi-Fi dependency: GCC host unit tests work |
| `pc/udp_cli.py` | Discovery, sequential requests, RTT, CSV | Runs on ordinary Python without Docker |
| `scripts/ap_start.sh` | NetworkManager AP profile and activation | Repeatable host setup |

## CMake and configuration

- Project `CMakeLists.txt` loads ESP-IDF CMake and declares the project.
- `main/CMakeLists.txt` uses `PRIV_REQUIRES` to declare dependencies on all four user components.
- Each component has `idf_component_register(SRCS ... INCLUDE_DIRS ... PRIV_REQUIRES ...)`.
- `main/Kconfig.projbuild` exposes SSID and password in `idf.py menuconfig`. These appear as `CONFIG_HIKOROBO_WIFI_SSID` and `CONFIG_HIKOROBO_WIFI_PASSWORD` in `sdkconfig.h` and are substituted into `wifi_manager.c`.
- `sdkconfig.defaults` selects ESP32-S3, 16-MB flash and custom partitioning, and reserves USB Serial/JTAG for commands (not stdout).
- `partitions.csv` provides NVS, PHY and a 4-MB factory application region; no OTA partitions yet. This is a starting point, not a flight-ready or OTA layout.
- `compose.yaml` bind-mounts `firmware/flight_controller` into `/project`; `compose.usb.yaml` exposes the USB device only for flashing.

## Key APIs

- `esp_netif_init()` — initialize network TCP/IP subsystem.
- `esp_event_loop_create_default()` — create default event loop.
- `esp_netif_create_default_wifi_sta()` — default Wi-Fi station netif, including DHCP client.
- `esp_wifi_init/start/connect()` — start/attach to Ubuntu hotspot.
- `IP_EVENT_STA_GOT_IP` — suitable point to open UDP sockets.
- `xEventGroupSetBits/WaitBits` — signal that DHCP gave a usable IP.
- `socket(AF_INET, SOCK_DGRAM, ...)`, `bind()`, `recvfrom()`, `sendto()` — standard BSD-style UDP interface exposed by ESP-IDF lwIP.
- `usb_serial_jtag_read_bytes/write_bytes` — wired fallback command channel.
- `atomic_int` — safe shared access to the rate variable between FreeRTOS USB and UDP tasks.

## Current limitations

- This is a **development demonstration**, not a flight controller or verified real-time link.
- No per-request ID: a delayed reply after an earlier timeout can be confused with a later reply (especially for repeated PING). Do not assume a returned response corresponds to the latest command in all failure scenarios.
- No authentication above WPA2, no cryptographic signing, no app-specific safety gate for commands.
- `esp_wifi_connect()` retries immediately after disconnect in this example; for production, add bounded retry/backoff and diagnostics.
- Logs are not sent to USB console because USB is used for the command protocol. `GET_WIFI` via the USB CLI helps debug without a second UART adapter.
- Changes to Wi-Fi credentials require `menuconfig`, rebuild and flash. Password exists in plaintext in generated config/binary; keep them private.
- Full ESP-IDF cross-compilation and USB-on-board tests must be run on the user's development machine with the official Docker image and hardware; host-side tests cannot validate wireless compatibility or timing.
