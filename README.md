# IR学習リモコン (ESP32-DEVKITC-VIE + VS1838B + SGN119)

既存の赤外線リモコン信号を学習し、シリアルコマンドで再送信するプロジェクト。

## 使用部品

- ESP32-DEVKITC-VIE
- VS1838B（38kHz赤外線受信モジュール、3pin: VCC/GND/OUT）
- SGN119（赤外線LED、940nm）
- 2SC1815L-GR（NPNトランジスタ、hFE 200-400、Ic max 150mA）
- 抵抗: ベース用 1kΩ、LED用 100Ω（電源電圧・LED順電圧により調整）

LED電流は (5V - LED順電圧約1.2〜1.5V - Vce(sat)約0.2V) / 100Ω ≈ 33〜36mA。2SC1815L-GRのIc上限150mAに対し余裕大きく、飛距離より安全マージン優先の設定。

## 配線

### VS1838B（受信）

| VS1838B | 接続先 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| OUT | GPIO27 |

多くのVS1838Bモジュールは内部プルアップ済みのオープンコレクタ出力なので、追加のプルアップ抵抗は基本不要。

### SGN119（送信、トランジスタ駆動）

38kHzキャリアをESP32のGPIOから直接大電流で流すのは非推奨のため、トランジスタでスイッチングする。

```
5V ---[SGN119 アノード]---[SGN119 カソード]---[抵抗100Ω]---[トランジスタ コレクタ]
                                                                  |
GPIO26 ---[抵抗1kΩ]---[トランジスタ ベース]                       |
                                                                  |
GND -------------------------------------------------------[トランジスタ エミッタ]
```

- トランジスタ: 2SC1815L-GR
- GPIO26がHIGHになるとベース電流が流れ、コレクタ-エミッタ間が導通しSGN119が点灯
- IRsendライブラリがGPIO26を38kHzでON/OFFし、赤外線パルスを生成する

## ライブラリ

`IRremoteESP8266`（PlatformIO lib_depsで自動取得）
NEC・SONY・Panasonicなど主要メーカーのプロトコルのデコード・再送信、および`IRac`クラスによるエアコンの汎用状態制御に対応。

## 使い方

1. PlatformIOでビルド・書き込み
2. `web/remote.html`をChromeで開きBLE接続（詳細は下記「BLE操作」参照）
3. リモコンとして操作、または学習

学習した信号・エアコン設定はいずれもNVS（フラッシュ内蔵不揮発メモリ）に保存されるため、電源を切っても保持される。

シリアルモニタ（115200bps）はデバッグログ出力用（`Serial.println`）のみで、コマンド入力機能は無い。すべての操作はBLE経由で行う。

## BLE操作（スマホから学習・送信）

ESP32内蔵BLEでスマホ(Web Bluetooth対応ブラウザ = Chrome)から操作できる。イベント駆動アーキテクチャ([DualGY521_M5Stamp](https://github.com/toshikinagahama/DualGY521_M5Stamp)の構成を踏襲):

- **FreeRTOSキューによるイベントバス**(`enqueue`/`dequeue`) — BLE書き込み・IR学習タスクが全てイベントとして一元集約される
- **Stateパターン**(`STATE_ADVERTISE` → `STATE_IDLE` → `STATE_LEARNING`) — 学習中は専用FreeRTOSタスクがIR受信をポーリングし、結果をイベントとして返す
- BLE通信は単一キャラクタリスティックで `write`(コマンド送信) / `notify`(結果通知) を行う、カテゴリ+種別の2バイトコマンド方式

### ファイル構成

```
include/common/MyState.h      状態ID定義
include/common/MyEvent.h      BLEコマンドコード・イベントID定義
include/common/MyGlobal.h     イベントキュー・グローバル参照
include/IrController.h        IR受信/送信/スロット永続化(NVS)
include/MyBLE.h                BLEサーバーラッパー
include/states/                State基底・StateManager・各State実装
web/remote.html                 Web Bluetooth操作画面
```

### BLEコマンド仕様

サービスUUID: `d9a1c1a0-1b0e-4f7e-9a2b-6f9a2f6e6c11`
キャラクタリスティックUUID: `d9a1c1a1-1b0e-4f7e-9a2b-6f9a2f6e6c11`
デバイス名: `IR-Remote-ESP32`

`write`するバイト列は `[カテゴリ, 種別, ...payload]`:

| コマンド | バイト列 | 説明 |
|---|---|---|
| GET_DEVICE_INFO | `0x00 0x01` | デバイス情報取得 |
| GET_SLOTS | `0x01 0x00` | 全16スロットの状態を取得(スロットごとにnotify) |
| SET_LABEL | `0x01 0x01 <slot> ...label(UTF-8)` | ボタン名設定(最大17バイト、BLEデフォルトMTU制約) |
| DELETE_SLOT | `0x01 0x02 <slot>` | スロット削除 |
| SET_CATEGORY | `0x01 0x03 <slot> <category>` | タブ分類変更(0=その他/1=テレビ/2=照明) |
| LEARN_START | `0x02 0x00 <slot>` | 学習開始(15秒タイムアウト) |
| LEARN_CANCEL | `0x02 0x01` | 学習キャンセル |
| SEND_SLOT | `0x03 0x00 <slot>` | 登録済み信号を送信 |

`notify`で返る文字列(カンマ区切りテキスト):

| 形式 | 説明 |
|---|---|
| `slot,<n>,<valid>,<label>,<protocol>,<bits>,<category>` | スロット状態(GET_SLOTS応答・学習成功・SET_LABEL/DELETE_SLOT/SET_CATEGORY確認) |
| `send,<n>,<0/1>` | 送信結果 |
| `learn,start,<n>` / `learn,unknown` / `learn,timeout` / `learn,cancelled` | 学習の進行状況 |
| `device_info,<text>` | デバイス情報 |

エアコン用コマンド(`acbrand,...` / `ac_state,...`)は後述「汎用エアコン制御(IRac)」参照。

### web/remote.html の使い方

1. Chromeで`web/remote.html`をローカルから開く（`file://`でも動作、Web Bluetoothはlocalhostまたはfile読み込みで動く場合が多いが、環境によっては簡易HTTPサーバー経由が必要な場合あり: `python3 -m http.server` 等）
2. 「BLE接続」タップ→ペアリングダイアログで`IR-Remote-ESP32`選択
3. 4タブ([エアコン][テレビ][照明][その他])が表示される。テレビ/照明/その他タブはリモコン風ボタングリッド(合計16スロット、詳細は後述)
4. 通常タップ = 送信、「編集モード」ONでタップ = 学習/名前変更/タブ分類変更モーダルが開く

### 汎用エアコン制御(IRac)

IRremoteESP8266の`IRac`クラスを利用し、学習不要でブランド選択→温度/モード/風量/スイングを直接操作できる。対応ブランドはコンパイル時のライブラリが対応する全プロトコル(60〜80種、Panasonic/Daikin/Mitsubishi/東芝/日立/シャープ等)を`IRac::isProtocolSupported()`で動的に列挙するため、ライブラリ更新でそのまま拡張される。

追加コマンド(カテゴリ0x04):

| コマンド | バイト列 | 説明 |
|---|---|---|
| GET_AC_BRANDS | `0x04 0x00` | 対応ブランド一覧取得(ブランドごとにnotify、`acbrand,<id>,<name>`) |
| SET_AC_BRAND | `0x04 0x01 <id_lo> <id_hi>` | ブランド選択(protocol_idはacbrand通知で受け取った値) |
| GET_AC_STATE | `0x04 0x02` | 現在のエアコン状態取得 |
| SET_AC_STATE | `0x04 0x03 <power> <mode> <temp> <fan> <swing>` | 状態を設定して即送信(mode: 0自動/1冷房/2暖房/3除湿/4送風、fan: 0〜6) |

状態(`ac_state,<power>,<protocol_id>,<protocol_name>,<mode>,<temp>,<fan>,<swing>`)はNVSに永続化され、BLE接続直後に`STATE_IDLE`進入時、登録済みスロット一覧と合わせて自動でHTML側にpushされる(明示的なGET要求は不要、`web/remote.html`側は再取得用に念のため送っている)。

**TV/ライトについて**: IRremoteESP8266に汎用TV・照明プロトコルは存在しない(メーカーごとにコード体系が異なり共通化不可)ため、学習スロット方式(16ボタン)をカテゴリ分けして代用する。

### タブ構成・カテゴリ分け

`web/remote.html`は4タブ構成: **エアコン**(IRac制御) / **テレビ** / **照明** / **その他**。学習スロットは8→**16個**に拡張し、各スロットに`category`(0=その他/1=テレビ/2=照明)を持たせてタブ振り分けする。

- 初回起動時のみ、スロット0-4=テレビ/5-9=照明/10-15=その他 をデフォルト割当し、以降はNVSに永続化(`SET_CATEGORY`で変更可能)
- 編集モーダルに「テレビ/照明/その他」選択を追加、名前保存と同時にカテゴリも更新される
- 追加コマンド: `SET_CATEGORY` (`0x01 0x03 <slot> <category>`)。`slotInfoLine`の末尾に`<category>`が追加された(`slot,<n>,<valid>,<label>,<protocol>,<bits>,<category>`)

### 既知の制約

- MITM/PINペアリングは未実装(Web Bluetoothとの相性を優先し暗号化なしのシンプル構成にした)。近くにいれば誰でも接続・操作できるため、屋外等で使う場合は注意
- ラベルは17バイト(UTF-8)まで。日本語だと5〜8文字程度が目安

## Alexa音声操作 (AWS IoT Core + Smart Homeスキル)

「アレクサ、テレビつけて」「アレクサ、エアコンを26度にして」のような音声操作に対応。BLEはスマホが近くにいる時専用なので、クラウド経由でどこからでも操作できるようにする狙い。

### アーキテクチャ

```
Echo端末 → Alexaクラウド → Smart Homeスキル(Lambda, aws/lambda/index.js)
                              → AWS IoT Core Device Shadow を更新(desired)
                                → ESP32(WiFi+MQTT/TLS常時接続)がdeltaを購読
                                  → 既存の enqueue() イベントバスに投入
                                  → StateIdle.cpp の既存ハンドラがそのまま処理
                                    (EVT_CMD_SEND_SLOT / EVT_CMD_SET_AC_STATE を再利用)
                                  → 処理後、Shadowのreportedを更新して応答
```

BLEコマンドと全く同じ「外部入力→`enqueue()`→既存State」の経路を通すため、State側の新規ロジックはほぼ無い。`AwsIotClient`はMQTT delta JSONを既存イベントへ変換するだけの薄いアダプタ。

### ESP32側の構成

| ファイル | 役割 |
|---|---|
| `include/WifiManager.h` / `src/WifiManager.cpp` | WiFi認証情報の永続化(NVS)・接続管理。`web/remote.html`からBLE経由で設定 |
| `include/AwsIotClient.h` / `src/AwsIotClient.cpp` | AWS IoT Core(Device Shadow)とのMQTT/TLS接続。専用FreeRTOSタスク(`awsIotTaskFunc`)が継続的に維持 |
| `include/secrets.h` | 証明書・秘密鍵・エンドポイント(gitignore対象、`secrets.h.example`がテンプレート) |

WiFi設定コマンド(カテゴリ0x05):

| コマンド | バイト列 | 説明 |
|---|---|---|
| SET_WIFI_SSID | `0x05 0x00 ...ssid(UTF-8)` | SSID設定 |
| SET_WIFI_PW | `0x05 0x01 ...password(UTF-8)` | パスワード設定 |
| GET_WIFI_STATUS | `0x05 0x02` | 接続状態取得 |
| CONNECT_WIFI | `0x05 0x03` | 保存済み認証情報で接続開始 |

`web/remote.html`下部の「WiFi設定」パネルから設定可能。接続状態は`wifi_status,<connected>,<ssid>,<ip>`としてnotifyされる。

### AWS側の構成

| リソース | 内容 |
|---|---|
| AWS IoT Core | Thing `ir-remote-esp32-01`(ap-northeast-1)。証明書のポリシーは`$aws/things/ir-remote-esp32-01/shadow/*`のみに限定 |
| Lambda | `ir-remote-alexa-skill`。**us-west-2**にデプロイ(Alexa Smart Homeスキルは極東/FEリージョンのLambdaをus-west-2にしか置けない制約があるため、IoT Core自体はTokyoのまま、LambdaのSDKクライアントだけリージョンを明示指定してクロスリージョンで呼んでいる) |
| Cognito User Pool | `ir-remote-alexa-users`。Alexaのアカウントリンク用OAuth2プロバイダ(`/oauth2/authorize`・`/oauth2/token`を利用、自前OAuthサーバーは書いていない) |
| Alexaスキル | Smart Homeスキル「IRリモコン」。`aws/skill-manifest.json`・`aws/account-linking.json`(テンプレート、実値は`clientSecret`のみ手動で埋める)。作成・デプロイは`ask smapi`(ASK CLI)で実施 |

### Lambda(`aws/lambda/index.js`)が処理するディレクティブ

- `Alexa.Discovery.Discover`: shadowのreported.slots(ラベル付きスロット一覧)を`Alexa.SceneController`エンドポイントとして返す。生の学習コードはトグル式の状態を持たないため`PowerController`ではなく`SceneController`(Activate系)を採用。加えてエアコン用に`ThermostatController`+`PowerController`を持つ固定エンドポイント`ac-1`を追加
- `Alexa.SceneController.Activate`: shadow desiredに`{sendSlot: n}`を書き込み
- `Alexa.ThermostatController.*` / `Alexa.PowerController.*`: shadow desiredに`{power, mode, temp, fan, swing}`(変更分のみ)を書き込み。Alexa標準の温度モードは`AUTO`/`COOL`/`HEAT`/`OFF`のみ対応(除湿・送風はAlexa標準インターフェースに無いため`AUTO`扱いにフォールバック)
- `Alexa.ReportState`: shadowのreportedを読んで返す

デプロイ:
```
cd aws/lambda
npm install --production
zip -r function.zip index.js node_modules package.json
aws lambda update-function-code --function-name ir-remote-alexa-skill --zip-file fileb://function.zip --region us-west-2
```

### 既知の制約(Alexa連携)

- shadow deltaは変更フィールドのみ届く。ESP32側は未指定フィールドを現在値で埋めてから適用する(`IrController::getAcWireState()`)。ここを間違えると「温度だけ変えたら電源が切れる」といった事故になる
- Alexa標準の`ThermostatController`は除湿(DRY)・送風(FAN)モードを持たないため、音声では冷房/暖房/自動/オフのみ操作可能(除湿等は`web/remote.html`側のみで操作)
- Cognitoユーザーは個人利用前提の単一アカウント。複数人で使う場合はユーザー追加が必要

## 回路図

EasyEDA Proで回路図を作成済み（プロジェクト名: `ir_remote_learn`）。
ESP32-DEVKITC-VIE・SGN119はライブラリに正確な型番が無いため、ピン配置・電気特性が同等の代替部品（ESP32-DevKitC汎用シンボル／IR323/H0-A）で作図している。

## 制限事項

- IRremoteESP8266が対応していないプロトコル、または家電メーカー独自のraw波形のみの信号は「未知プロトコル」となり保存できない（現状raw波形保存には非対応、必要なら拡張可）
- エアコンなど長いデータ長・状態型プロトコル（Panasonic ACなど）は学習スロット側でも`state`配列を使い正しく保存・再送信できる。加えて`IRac`クラスによる汎用エアコン制御(ブランド選択+温度/モード/風量操作)も別途搭載済み（詳細は「汎用エアコン制御(IRac)」参照）
