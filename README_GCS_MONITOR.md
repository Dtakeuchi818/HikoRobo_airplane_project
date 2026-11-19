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

### 姿勢推定

XIAOは補正済みの角速度と加速度を使って、現在のIMU読み取り周期（約10 Hz）で姿勢を推定します。「姿勢フィルタ」の「XIAOの設定を取得」で現在値を読み込み、アルゴリズムとパラメータを選んで「設定を適用」を押します。「ライブ表示開始」でロール・ピッチ・ヨーの直近10秒を度単位で表示します。

- 相補フィルタの **τ** はジャイロで積分した姿勢を加速度の傾きに戻す時間定数です。大きい値ほど補正が緩やかです。初期値は 0.5 s です。
- Mahonyフィルタの **Kp** は傾き誤差への即時の反応、**Ki** は誤差の積分補正の強さです。初期値は Kp = 1.0、Ki = 0.05 です。Ki = 0 で積分補正を無効にできます。
- 加速度の大きさが 0.8〜1.2 g の間にあるときに傾き補正に使います。ヨーは初期姿勢を 0° とした相対値で、磁気センサーによる方位基準がないためドリフトします。起動時およびサンプル間隔が0.5秒を超えた後は、次の有効な加速度で姿勢を初期化します。

「IMUの3D姿勢」では、選択した基準のXY平面をグリッドとして表示します。破線は基準のXYZ軸、実線はIMUとともに回転するXYZ軸です。赤がX、緑がY、青がZです。姿勢データの受信が止まると、IMUの軸を非表示にします。

設定はXIAOのRAMに保持されます。フィルタやパラメータを変更した場合は姿勢を引き継ぎ、Mahonyの積分項をクリアします。XIAOの再起動後は相補フィルタと初期値に戻ります。

### 姿勢の基準を設定

任意の向きで「現在の姿勢を基準に設定」を押すと、XIAOが最新の有効な姿勢を保存します。「設定した基準」ではその時点のロール・ピッチ・ヨーを0°として表示します。「地面基準」に切り替えると元の角度を確認できます。3D表示では、選択した基準のXYZ軸を W X・W Y・W Z として描画します。「設定した基準」では、保存時のIMU軸が W 軸と一致します。

基準の設定はフィルタや静止時バイアス補正に影響しません。「基準を解除」、姿勢推定の再初期化、XIAOの再起動で基準は消去されます。

[IMUストリームのプロトコル](shared/protocol/imu_stream_v1.md)

## S.BUS ライブ表示

1. XIAO D7（GPIO44）に S.BUS 信号を接続します。`menuconfig` で `HikoRobo S.BUS monitor` の `Boot into the S.BUS USB monitor` を無効にして通常起動に戻します。
2. IMU と同じ `Ground PC IPv4 address` を使います。すでに IMU がライブ表示できる場合は設定済みです。ファームウェアを書き込み、GCS を `docker compose -f compose.gcs.host.yaml up -d --build gcs` で再ビルドします。
3. ブラウザの「通信設定」で XIAO の IPv4 を指定し、「グラフ表示」→「S.BUS チャンネル」→「ライブ表示開始」を押します。

CH1〜8 の生値（0〜2047）が横並びの棒グラフに表示されます。受信停止から約 1 秒で棒がクリアされます。

「変換パラメータ設定」で CH を選び、「取得」で XIAO の設定を読み込みます。最小値・中央値・最大値はライブ表示中にスティックを動かして取り込めます。モードごとの角度下限・上限（°）を入力し、「適用」を押すと XIAO の次の受信フレームから反映されます。上下限とも 0° の CH は角度指令を無効にします。

「モード切り替え設定」でスイッチの CH と、各モードに対応する重複しない生値の範囲を適用します。切り替え CH は角度指令から外れます。範囲外や受信異常では指令が無効になり、同じ範囲に約 100 ms 入るとモードが確定します。初期状態では切り替えが無効で角度上下限も 0° です。「適用済み設定を XIAO に保存」を押すと、再起動後に復元されます。角度グラフの中央線は 0° です。

設定は UDP 5005 の `SBUS GET_CH`、`SBUS SET_CH`、`SBUS GET_MODE`、`SBUS SET_MODE`、`SBUS SAVE` で送受信します。テレメトリーは UDP 5006 で `HK2 SBUS <lost> <failsafe> <ch1> ... <ch8> <mode> <mask> <norm1> <angle1> ... <norm8> <angle8>` を配信します。モードは無効・舵面・目標姿勢の順に 0・1・2、マスクの最下位ビットから CH1〜8 の角度の有効性を示します。正規化値は千分率、角度は 0.01° 単位の整数です。

### ビルド時の初期値

[sbus_defaults.json](firmware/flight_controller/sbus_defaults.json) を編集して、通常の手順でファームウェアをビルド・書き込みします。ビルド時に値の範囲と順序を検証します。

「S.BUS チャンネル」の「ビルド用 JSON をダウンロード」からも作成できます。接続先の XIAO からモード設定と全 CH の適用済み設定を取得し、画面で編集中のモード設定と選択中の CH の変更もファイルに反映します。ダウンロードした `sbus_defaults.json` を `firmware/flight_controller/` に配置してビルドしてください。

- `mode.switch_channel` はモード切り替え用の CH 番号（1〜8）です。0 で切り替えを無効にします。
- `mode.surface_raw` と `mode.attitude_raw` はそれぞれ舵面直接操作と目標姿勢指令に対応する生値の `[最小, 最大]` です。両端を含み、範囲は重複できません。
- 各 CH の `raw` は `[最小, 中央, 最大]` です。
- 各 CH の `surface_deg` と `attitude_deg` はそれぞれのモードの角度 `[下限, 上限]` です。度単位で、−20°〜+25° は `[-20, 25]` と書きます。小数は第2位まで指定できます。旧形式の `*_centideg` も 0.01° 単位として読み込めます。

JSON の設定値を変更してファームウェアを書き込むと、次の起動時から新しい値が使われます。ブラウザから保存した設定は、保存時と起動時でビルド時の初期値が一致する場合に復元します。旧形式の保存値がある機体では、更新後の初回起動時に JSON の値が読み込まれます。GUI の「取得」で現在値を確認できます。

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
