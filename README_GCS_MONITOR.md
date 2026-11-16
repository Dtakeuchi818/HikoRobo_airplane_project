# HikoRobo GCS 0.4 — Web GUI / UDP品質・状態監視・USB切替

前回の `hikorobo_gcs_commands_patch.zip` と、正常動作確認済みの `compose.gcs.host.yaml` を前提にした差分パッチです。

## 実装内容

- UDP/USB切替、接続確認・切断、USBポート一覧更新、UDPブロードキャスト探索
- PING / GET_STATUS / GET_WIFI / GET_RATE / SET_RATE（既存互換APIを維持）
- ブラウザが開いている間、2/3/5/10秒周期のPINGで接続状態を監視
- UDP品質測定（5〜100回、50〜1000ms間隔、RTT中央値/P95/min/max、未応答率、RTT変動、グラフ、CSV保存）
- 通信ログの表示・テキスト保存、設定のブラウザ内保存
- すべて既存のXIAOコマンドを使用。**IMU/SBUSストリーム送信・WebSocketは今回使用しない。**

## 導入：まずUDPだけ

```bash
cd ~/Robocon_projects/HikoRobo_airplane_project
cp -a pc/src/hikorobo_gcs/server/app.py pc/src/hikorobo_gcs/server/app.py.bak
cp -a pc/src/hikorobo_gcs/server/udp_commands.py pc/src/hikorobo_gcs/server/udp_commands.py.bak
# ダウンロードしたZIPを展開（ファイルの上書きを伴います）
unzip -o ~/Downloads/hikorobo_gcs_monitor_patch.zip -d .
docker compose -f compose.gcs.host.yaml up -d --build gcs
curl http://127.0.0.1:8000/api/health
```

- GUI： http://127.0.0.1:8000/
- API一覧： http://127.0.0.1:8000/docs
- Host版は `network_mode: host` であり、**Uvicornは `--host 127.0.0.1 --port 8000`** のままにすること。LANへ認証なしで公開しない。
- 初期状態は通信未確認。IP確認後、「接続確認」を押すとPINGで疎通を判定し、監視を開始。
- `GET_WIFI` はXIAO側のWi-Fi接続状態/IPを返すだけ。PCのRSSIやXIAOの電波強度ではない。
- 画面の品質測定はPINGの応答なし率であり、厳密な無線のパケット損失率ではない。別の端末やネットワーク負荷、タイムアウト値に依存する。

## USBも使用する場合

`pc/requirements-gcs.txt` に `pyserial` を追加済み。以前のDockerfileがこのファイルを使うので、以下の `--build` で再構築する。

```bash
cd ~/Robocon_projects/HikoRobo_airplane_project
# 接続デバイスの例 /dev/ttyACM0 を確認
ls -l /dev/ttyACM*
# .env に ESP_PORT と DIALOUT_GID があることを確認
cat .env
# 現在のGCSを一度停止（通常は既存ファイルで起動）
docker compose -f compose.gcs.host.yaml down
# USBデバイスを渡して起動
docker compose -f compose.gcs.host.yaml -f compose.gcs.usb.yaml up -d --build gcs
```

`.env` の例（すでに設定していれば変更不要）：

```dotenv
ESP_PORT=/dev/ttyACM0
DIALOUT_GID=20
```

`DIALOUT_GID` は固定値20と決め打ちせず、`getent group dialout | cut -d: -f3`で確認する。
既存の`.env`は**上書きしない**こと（LOCAL_UIDなど別の定義があるため）。

- USB Serial/JTAGを使用するため、ESP-IDFのシリアルモニターなどがポートを占有している場合は終了する。
- USBで動作させるときはGUIで「USB Serial」を選択し、`/dev/ttyACM0`などのポートを指定後、「接続確認」を押す。
- USBポートは接続中は保持され、切断で閉じる。Web GUIではUSB経由のRTTと状態監視はできるが、**ネットワーク品質測定はUDP専用**。
- `/dev/ttyACM0`が消えて別番号になった場合は`.env`とGUIの入力を更新してコンテナを再起動する。
- Windows/macOS Docker Desktop上でのUSBパススルーはUbuntuと異なる。今回のoverlayはUbuntu Linux用。

## APIの例

```bash
# UDP PING（従来のAPIも互換）
curl -sS http://127.0.0.1:8000/api/ping -H 'Content-Type: application/json' -d '{"host":"10.42.0.162"}'

# UDP品質測定：10回、200ms間隔
curl -sS http://127.0.0.1:8000/api/quality -H 'Content-Type: application/json' -d '{"transport":"udp","host":"10.42.0.162","port":5005,"timeout_sec":1,"count":10,"interval_ms":200}'

# 接続確認
curl -sS http://127.0.0.1:8000/api/connect -H 'Content-Type: application/json' -d '{"transport":"udp","host":"10.42.0.162"}'
```

## 注意点

- Uvicornは **1ワーカー** で実行すること。USBの状態と通信排他ロックはコンテナ内1プロセスで保持されるため、`--workers 2`などにしないこと。
- すべてのコマンド、監視PING、品質測定に単一ロックを使用。品質測定中はコマンドがHTTP 409（処理中）になることがある。画面上は品質測定中にボタンを無効化する。
- 受信タイムアウトは0.1〜3秒、品質測定は最大100回。タイムアウト設定が短すぎると未応答率が増える。
- ブラウザを閉じると監視は停止。サーバー側で24時間の常時監視はしていない。
- 他のブラウザが同じUSBポートや接続状態を操作すると干渉する可能性があるため、まず1クライアントで使用。
- USB接続はXIAO/USB Serial/JTAG構成により再接続時に再起動や一時的な出力が発生する場合がある。
- センサ受信・ストリーム送信は未実装。

## トラブルシュート

```bash
docker compose -f compose.gcs.host.yaml logs --tail=100 gcs
# USBで起動している場合：
docker compose -f compose.gcs.host.yaml -f compose.gcs.usb.yaml exec gcs python -c 'from serial.tools import list_ports; print(list(list_ports.comports()))'
```

画面が古い場合は `Ctrl+Shift+R` で更新する。

## テスト

モックXIAO UDPサーバーを用いたFastAPI APIテストを付属しています。開発環境にpytestとhttpxが必要です。

```bash
PYTHONPATH=pc/src python -m pytest -q pc/tests/test_gcs_monitor.py
```
