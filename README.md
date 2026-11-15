# HikoRobo: Step 2 USB serial communication

ESP-IDF v6.1.1 / XIAO ESP32S3 Plus / Python pyserial / Ubuntu 24.04.

This is a minimal **starter snapshot** of the monorepo layout. Merge into your existing repository; back up any existing files before copying. `hardware/`, `docs/` and full ground-station GUI can be added later.

## Layout

- `firmware/flight_controller/`: ESP-IDF application.
- `firmware/flight_controller/components/usb_transport/`: USB Serial/JTAG byte transport / LF framing.
- `firmware/flight_controller/components/command_protocol/`: transport-independent command handler.
- `pc/`: Python CLI; installed with pip in a venv.
- `shared/protocol/`: protocol definition.
- `compose.yaml`: build environment; `compose.usb.yaml`: device pass-through for flashing.

## 1. Environment

On Ubuntu 24.04, ensure Docker Engine + Compose and Python venv are available. Work from **repository root**.

```bash
cat > .env <<EOF
LOCAL_UID=$(id -u)
LOCAL_GID=$(id -g)
DIALOUT_GID=$(stat -c %g /dev/ttyACM0)
ESP_PORT=/dev/ttyACM0
EOF
```

Connect XIAO by a data-capable USB cable. Change port if necessary (`ls /dev/ttyACM* /dev/ttyUSB*`). `DIALOUT_GID` is the **actual group ID** of the device; it can differ by OS.

```bash
docker compose config
docker compose run --rm idf idf.py --version
```

## 2. ESP-IDF config and build

**Important:** This project uses the USB Serial/JTAG peripheral **directly** from the application driver, while the console is configured as `NONE`. This prevents boot/application logs from mixing into command responses. Unlike the previous Hello World, `idf.py monitor` is not the primary testing client. The `.env` file is untracked.

If you previously built the Hello World with `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, regenerate the ESP-IDF `sdkconfig` to apply this project's settings. Back up modified settings first. `set-target` also regenerates the build directory.

```bash
docker compose run --rm idf idf.py set-target esp32s3
docker compose run --rm idf idf.py build
```

Confirm:

```bash
grep '^CONFIG_ESP_CONSOLE_NONE=y' firmware/flight_controller/sdkconfig
```

## 3. Flash over USB

Make sure no Python serial client or `idf.py monitor` is using the port. Run:

```bash
docker compose -f compose.yaml -f compose.usb.yaml run --rm idf \
  idf.py -p /dev/ttyACM0 flash
```

The port may disappear/reappear during reset. If needed, use BOOT + RESET, recheck the port and update `.env`. If the port name changed, also change `-p`.

## 4. Python on Ubuntu host

```bash
sudo apt install python3-venv
python3 -m venv pc/.venv
pc/.venv/bin/python -m pip install -e ./pc
pc/.venv/bin/hikorobo-serial --list
pc/.venv/bin/hikorobo-serial --port /dev/ttyACM0 --self-test
pc/.venv/bin/hikorobo-serial --port /dev/ttyACM0
```

Interactive commands: `PING`, `GET_STATUS`, `GET_RATE`, `SET_RATE 250`, `HELP`. Use `quit` to end. For USB Serial/JTAG, 115200 is only a host-side serial API setting, not the physical USB bit rate.

If Python cannot access the serial port, check `ls -l /dev/ttyACM0` and `groups`. For standard Ubuntu permissions `sudo usermod -aG dialout "$USER"`, then log out and back in. Do not use the `docker` group as a substitute for `dialout`.

## 5. Optional non-hardware tests

The C parser is independent of ESP-IDF, so it can be compiled on Ubuntu:

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/flight_controller/components/command_protocol/include \
  firmware/flight_controller/components/command_protocol/command_protocol.c \
  firmware/flight_controller/tests/test_command_protocol.c -o /tmp/hikorobo_command_test
/tmp/hikorobo_command_test
```

Python test (after installing `pc/`):

```bash
pc/.venv/bin/python -m unittest discover -s pc/tests -v
```

## Troubleshooting

- `idf.py build` fails in `esp_driver_usb_serial_jtag`: check ESP-IDF version and component dependencies; this snapshot targets v6.1.1.
- Python times out: verify `CONFIG_ESP_CONSOLE_NONE=y`, use `flash` after config change, close `idf.py monitor`, try reset and reconnect USB, check serial port permissions.
- USB device missing: use a data-capable cable, check `lsusb` / `dmesg`, enter BOOT download mode if needed.
- `Device or resource busy`: only one host process may own the serial port at a time.
- `serial: permission denied`: add your user to the actual USB device group, log out/back in.
- USB unplug/replug may change `/dev/ttyACM0` and invalidate Docker's device mapping; create a fresh container for flashing.

This project is a **bench test**, not flight-ready firmware. No SBUS / servo / flight-control outputs are activated.
