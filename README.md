# HikoRobo Airplane Project — Step 3: Ubuntu AP + Wi-Fi/UDP

XIAO ESP32S3 Plus / ESP-IDF v6.1.1 / Ubuntu 24.04 / Docker Compose / Python.
**Step 2 USB command channel is kept**, and Step 3 adds Wi-Fi station + UDP command server.
Read [docs/architecture.md](docs/architecture.md) and [shared/protocol/udp_v1.md](shared/protocol/udp_v1.md) for design details.

> The commands below are run in the **repository root** (`HikoRobo_airplane_project/`).
> Hardware flashing and Wi-Fi tests were **not executed** in the generation environment; local GCC/Python tests and loopback UDP tests were executed successfully.

## 1. Initial Ubuntu setup

Install NetworkManager (`nmcli`), Python 3.10+, Docker Engine + Compose, a working 2.4-GHz AP-capable Wi-Fi adapter, and optionally `iw`.

```bash
nmcli device status
# Optional check: `iw list` -> Supported interface modes: AP
# Example Wi-Fi device: wlp3s0 (replace everywhere with YOUR actual interface)
```

The hotspot commandeers that Wi-Fi interface; it commonly disconnects an existing Wi-Fi connection. A wired Ethernet uplink is optional. The PC does NOT need an internet connection for local UDP.

## 2. Start Ubuntu hotspot

```bash
bash scripts/ap_start.sh wlp3s0
```

When prompted, enter a unique 8–63 character WPA2 password (ASCII recommended for this demo). It is saved in a local NetworkManager connection profile named `HikoRobo-Hotspot`; SSID is `HikoRobo_Lab` and frequency is 2.4 GHz.

The script runs `nmcli device wifi hotspot`, configures NetworkManager IPv4 `shared` mode for its small DHCP server, and prints the interface IP. The typical IP is `10.42.0.1/24`, but use **the address your PC actually shows**, not an assumed one. The broadcast address for `10.42.0.0/24` is `10.42.0.255`.

```bash
bash scripts/ap_status.sh wlp3s0
ip -4 addr show dev wlp3s0
nmcli -g ipv4.method connection show HikoRobo-Hotspot
```

The connection can be stopped/restarted using:

```bash
bash scripts/ap_stop.sh
bash scripts/ap_start.sh wlp3s0
```

On subsequent `ap_start.sh` calls, the saved NetworkManager connection is reactivated without asking for the password again. If the password changes, update the saved NetworkManager profile **and the ESP-IDF menuconfig**.

## 3. Prepare Docker environment

Copy `.env.example` to `.env` and set your PC's UID/GID, serial-device GID and USB port. On Ubuntu a convenient one-time command is:

```bash
cat > .env <<EOF_ENV
LOCAL_UID=$(id -u)
LOCAL_GID=$(id -g)
DIALOUT_GID=$(stat -c %g /dev/ttyACM0)
ESP_PORT=/dev/ttyACM0
EOF_ENV
```

Change `/dev/ttyACM0` if needed. This command requires the device to be connected already. Do not commit `.env`.

```bash
docker compose config
docker compose run --rm idf idf.py --version
```

## 4. Set ESP32-S3 target and Wi-Fi credentials

**Important order:** `set-target` regenerates `sdkconfig`. Do it **before** entering Wi-Fi secrets.

```bash
docker compose run --rm idf idf.py set-target esp32s3
docker compose run --rm idf idf.py menuconfig
```

Under **HikoRobo Wi-Fi settings**, enter:

- SSID: `HikoRobo_Lab`
- Wi-Fi password: exactly the password entered in `scripts/ap_start.sh`

Save and exit. The credentials live in `firmware/flight_controller/sdkconfig`, which is gitignored. They are also embedded in the compiled firmware; don't publish binaries. Avoid `idf.py set-target` after configuring them unless you plan to re-enter them.

The firmware uses a custom partition table `firmware/flight_controller/partitions.csv` with a 4MB factory app region, to allow for ESP-IDF Wi-Fi libraries. No OTA setup is required in this exercise.

Verify effective config without printing secrets:

```bash
grep -E '^CONFIG_(IDF_TARGET|PARTITION_TABLE_CUSTOM|ESP_CONSOLE_NONE)=' firmware/flight_controller/sdkconfig
```

## 5. Build and flash

```bash
docker compose run --rm idf idf.py build

docker compose -f compose.yaml -f compose.usb.yaml run --rm idf \
  idf.py -p /dev/ttyACM0 flash
```

The firmware starts USB command processing and tries to connect to the Ubuntu hotspot automatically. On IP acquisition, it starts a UDP server on UDP port 5005.

## 6. Setup Python (runs directly on Ubuntu)

```bash
python3 -m venv pc/.venv
pc/.venv/bin/python -m pip install -e ./pc
```

On Ubuntu 24.04, install `python3-venv` first if missing. The Python UDP client only uses the standard library, but installing the package also enables the old `hikorobo-serial` USB tool.

### Check Wi-Fi connection over USB

```bash
pc/.venv/bin/hikorobo-serial --port /dev/ttyACM0
```

In the terminal, enter `GET_WIFI`. Repeat after a few seconds if needed:

```text
XIAO> GET_WIFI
OK WIFI CONNECTED IP=10.42.0.23
```

If it returns `OK WIFI DISCONNECTED`, check SSID/password, 2.4GHz hotspot and its NetworkManager status. A wrong password causes retries. Don't run `idf.py monitor` simultaneously with the Python USB CLI.

### Discover ESP32 by UDP broadcast

On a normal `10.42.0.0/24` hotspot:

```bash
pc/.venv/bin/hikorobo-udp --discover --broadcast 10.42.0.255
```

If your Ubuntu AP subnet differs, use its actual broadcast address from `ip -4 addr show dev <interface>`.

Enter commands:

```text
XIAO-UDP> PING
OK PONG [3.1 ms]
XIAO-UDP> GET_STATUS
OK STATUS READY RATE=100 [3.0 ms]
XIAO-UDP> SET_RATE 250
OK RATE 250 [4.1 ms]
XIAO-UDP> GET_RATE
OK RATE 250 [3.2 ms]
XIAO-UDP> quit
```

These are ILLUSTRATIVE output values, not measured ESP32 results.

Or use the IP from `GET_WIFI` directly:

```bash
pc/.venv/bin/hikorobo-udp --host 10.42.0.23 --self-test
```

### Measure UDP request/response round-trip times

```bash
pc/.venv/bin/hikorobo-udp --discover --self-test
pc/.venv/bin/hikorobo-udp --discover --benchmark 100 --interval 0.05 \
  --timeout 2 --csv data/udp_rtt.csv
```

The benchmark records UTC PC time, sequence, whether a reply was received, and RTT in ms; prints min/median/p95/max and unanswered rate. This **is not** a guaranteed wireless packet-loss metric (which direction lost a datagram is unknown).

## 7. Test without XIAO or Wi-Fi hardware

The mock server listens only on localhost; it simulates the textual protocol, not ESP-IDF or radio behavior.

Terminal A:

```bash
PYTHONPATH=pc/src python3 pc/tests/mock_udp_server.py --port 5005
```

Terminal B:

```bash
PYTHONPATH=pc/src python3 -m hikorobo_gcs.udp_cli --host 127.0.0.1 --self-test
PYTHONPATH=pc/src python3 -m hikorobo_gcs.udp_cli --host 127.0.0.1 --benchmark 20
```

Stop the mock server with `Ctrl+C`.

Automated host tests:

```bash
# C command parser, using host GCC, without ESP-IDF hardware libraries
gcc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/flight_controller/components/command_protocol/include \
  firmware/flight_controller/components/command_protocol/command_protocol.c \
  firmware/flight_controller/tests/test_command_protocol.c \
  -o /tmp/hikorobo_command_test
/tmp/hikorobo_command_test

# Python unit tests
PYTHONPATH=pc/src python3 -m unittest discover -s pc/tests -v
```

## 8. Reconnection test

1. Run `hikorobo-udp --discover --benchmark 100 --interval 0.2 --timeout 0.5`.
2. In another terminal, run `bash scripts/ap_stop.sh` to interrupt the hotspot.
3. Confirm some requests time out (a non-zero exit from the benchmark is expected during the outage).
4. Run `bash scripts/ap_start.sh wlp3s0` again.
5. Run USB CLI `GET_WIFI` until it reports `OK WIFI CONNECTED IP=...`.
6. Run `hikorobo-udp --discover --self-test` again. It should recover **without reflashing the XIAO**.

## 9. Troubleshooting

| Symptom | What to check |
|---|---|
| `nmcli` fails to create hotspot | Is the Wi-Fi interface correct and AP mode supported? Does NetworkManager manage it? |
| XIAO reports WIFI DISCONNECTED | Exact SSID/password in `menuconfig`, 2.4GHz, AP active, Wi-Fi power/current |
| `GET_WIFI` doesn't respond over USB | Firmware build/flash, correct ttyACM port, serial permissions, conflicting monitor |
| Broadcast discovery fails but GET_WIFI shows IP | Try `--host <XIAO_IP>`; verify `ip -4 addr`, broadcast subnet, AP isolation and host firewall |
| `--host` fails | Confirm UDP port 5005, client and server IP, radio link, firewall, firmware status |
| Firmware compiles but cannot flash | Check USB cable, `/dev/ttyACM*`, `.env` GID, device reset/BOOT mode |
| CMake still uses old partition/console config | `sdkconfig` may be stale; check effective settings, then regenerate carefully and re-enter password |

## 10. Security and flight-safety note

This is a **learning/test** protocol. It has no application-layer authentication or request IDs. UDP may lose, duplicate or reorder packets. Do NOT allow commands received through this network to actuate a flight controller without authentication, explicit armed/disarmed state handling, RC failsafe and a separate hard real-time control loop. Flight tests require additional risk controls.

## Source references

- ESP-IDF Station Scenarios: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/station-scenarios.html
- ESP-IDF lwIP Sockets: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/lwip.html
- NetworkManager nmcli: https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nmcli.html
