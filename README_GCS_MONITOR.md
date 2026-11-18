# HikoRobo GCS 0.4 — Web GUI / UDP品質・状態監視・USB切替

前回の `hikorobo_gcs_commands_patch.zip` と、正常動作確認済みの `compose.gcs.host.yaml` を前提にした差分パッチです。

## 実装内容

- UDP/USB切替、接続確認・切断、USBポート一覧更新、UDPブロードキャスト探索
- PING / GET_STATUS / GET_WIFI / GET_RATE / SET_RATE（既存互換APIを維持）
- ブラウザが開いている間、2/3/5/10秒周期のPINGで接続状態を監視
- UDP品質測定（5〜100回、50〜1000ms間隔、RTT中央値/P95/min/max、未応答率、RTT変動、グラフ、CSV保存）
- 通信ログの表示・テキスト保存、設定のブラウザ内保存
- 既存のXIAOコマンドに加え、IMU生値をUDPで受信し、WebSocketでライブ波形を表示できます。

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

## IMUライブ波形

1. `docker compose run --rm idf idf.py menuconfig` を開き、`HikoRobo IMU telemetry` の `Ground PC IPv4 address` にPCのWi-Fi側IPv4を設定します。Ubuntuホットスポットでは、`scripts/ap_status.sh` に表示されるPCのIPを使います。空欄では配信は無効です。
2. [READMEの手順](README.md#5-build-and-flash)でビルド・書き込みを行います。
3. `docker compose -f compose.gcs.host.yaml up -d --build gcs` でGCSを起動します。ファイアウォールを使用している場合は、機体が接続するインターフェースからのUDP 5006を許可します。
4. ブラウザで `http://127.0.0.1:8000` を開き、接続設定にXIAOのIPv4を入力して「IMU ライブ波形」の「ライブ表示開始」を押します。IPは既存の探索や `GET_WIFI` で確認できます。

加速度と角速度の各3軸を、生カウントと換算値（m/s²・°/s）の計4グラフで表示します。各グラフで縦軸の範囲を個別に設定できます。横軸はXIAOの取得時刻を基準とした直近10秒です。配信は現行の読み取り周期（約10 Hz）です。従来の単発・ポーリング取得とCSV保存は「IMU 生データ」欄から使えます。

### 静止時バイアス補正

機体を停止し、IMUの1軸が鉛直になるように置きます。GUIで上向きの符号付き軸（例 `+Z`）を選び、「静止時補正を開始」を押します。XIAOが有効な30サンプルを集め、通常は約3秒で完了します。動きや傾き、有効なサンプルの不足で中止した場合、適用中の補正は維持されます。

角速度は各軸の静止平均を差し引きます。加速度は指定した軸の ±9.80665 m/s² を期待値とし、各軸の静止平均と期待値との差を差し引きます。補正はXIAOのRAMに保持され、`imu_monitor_get_measurement()` とUDPの換算値に適用されます。「補正を解除」またはXIAOの再起動で初期状態に戻ります。ブラウザからは「状態を確認」でXIAOの設定を確認できます。

[IMUストリームのプロトコル](shared/protocol/imu_stream_v1.md)

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
