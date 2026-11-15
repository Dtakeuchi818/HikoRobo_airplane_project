# HikoRobo Airplane Project

XIAO ESP32S3 Plusを使用した
模型飛行機の飛行制御・データ収集プロジェクト。

## Development Environment

- Ubuntu 24.04
- Docker Engine / Docker Compose
- ESP-IDF v6.1.1
- Python（今後追加）

## Firmware Build

docker compose run --rm idf idf.py build

## Flash

docker compose \
  -f compose.yaml \
  -f compose.usb.yaml \
  run --rm idf \
  idf.py -p /dev/ttyACM0 flash

## Monitor

docker compose \
  -f compose.yaml \
  -f compose.usb.yaml \
  run --rm idf \
  idf.py -p /dev/ttyACM0 monitor
