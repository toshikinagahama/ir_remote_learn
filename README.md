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

- `Alexa.Discovery.Discover`: shadowのreported.slots(ラベル・カテゴリ付きスロット一覧)をカテゴリ別にエンドポイント化して返す
  - `category=2`(照明): `Alexa.PowerController`エンドポイントとして公開。「〜つけて」「〜けして」の発話に対応
  - それ以外(テレビ/その他): 従来通り`Alexa.SceneController`(Activate系)エンドポイントとして公開
  - 加えてエアコン用に`ThermostatController`+`PowerController`を持つ固定エンドポイント`ac-1`を追加
- `Alexa.SceneController.Activate`: shadow desiredに`{sendSlot: "<n>-<timestamp>"}`を書き込み(タイムスタンプを混ぜる理由は下記「AWS IoT Shadowのdelta設計」参照)
- `Alexa.PowerController.*`(`slot-n`宛て、照明用): `TurnOn`/`TurnOff`どちらが来てもshadow desiredに`{sendSlot: "<n>-<timestamp>"}`を書き込むだけ。1ボタントグル式の照明リモコンを学習させている前提のため、ON/OFFで信号を出し分けられない(下記「既知の制約」参照)
- `Alexa.PowerController.*`(`ac-1`宛て) / `Alexa.ThermostatController.*`: shadow desiredに`{power, mode, temp, fan, swing, cmdId}`(変更分のみ+cmdId)を書き込み。Alexa標準の温度モードは`AUTO`/`COOL`/`HEAT`/`OFF`のみ対応(除湿・送風はAlexa標準インターフェースに無いため`AUTO`扱いにフォールバック)
- `Alexa.ReportState`: shadowのreportedを読んで返す(照明の`powerState`は`retrievable: false`のため対象外)

デプロイ:
```
cd aws/lambda
npm install --production
zip -r function.zip index.js node_modules package.json
aws lambda update-function-code --function-name ir-remote-alexa-skill --zip-file fileb://function.zip --region us-west-2
```

### BLE / クラウド(Alexa)の排他運用

**BLEとAWS IoT(TLS)を同時に稼働できない。** ESP32(PSRAM非搭載、WROOM系)は、BLEサーバー+WiFiスタック+mbedTLSのフルTLSハンドシェイクを同時に賄うだけのヒープが無い。原因は主に2つ:

- BLEスタックがヒープを常時50〜70KB程度確保する
- このプロジェクトが使うPlatformIOのプリコンパイル済みmbedTLSは`MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH`が無効設定でビルドされており、TLSセッション中は送受信バッファを**固定16KB×2=32KB**確保し続ける(MQTTペイロードが数百バイトでも関係ない)。プリコンパイル済みバイナリのためコード側から縮小不可

これらを足すとBLE+WiFi+TLSの同時稼働に必要なメモリが常時不足し、`X509 - Certificate verification failed`や`SSL - Memory allocation failed`が安定して発生する(一時的な負荷ではなく恒常的な不足)。

**対策として排他モードを実装**:

- 起動時は常に**BLEモード**がデフォルト(今まで通りの動作、AWS IoTへは接続しない)
- `web/remote.html`のWiFi設定パネルから「クラウドモードへ切替」を実行すると、`BLEDevice::deinit()`でBLEスタックを完全停止しメモリを解放→AWS IoT(TLS)接続を開始する
- クラウドモードからBLEモードに戻すには**本体の電源を入れ直す**(電源再投入)必要がある。ソフトウェア的な自動復帰は無い(起動時に常にBLEモードへ戻る設計のため)

追加コマンド: `ENABLE_CLOUD_MODE` (`0x05 0x04`、payload無し)。切替直前に`mode,cloud_switching`をnotifyしてからBLEを停止する。

### AWS IoT Shadowのdelta設計(ハマった落とし穴)

Alexa音声操作の実装中、「1回目のコマンドは効くが2回目以降は無反応」「逆に同じ信号を無限に送り続けてしまう」という2つの重大な不具合を踏んだ。原因はAWS IoT Device Shadowの`delta`計算の仕様理解不足で、対策は`AwsIotClient::processDelta()`(`src/AwsIotClient.cpp`)に反映済み。同様の仕組みを拡張する際は必ずこの節を読むこと。

**仕様**: `delta`(`$aws/things/.../shadow/update/delta`で配信される内容)は「`desired`と`reported`で**値が実際に異なる**フィールドのみ」を含む。フィールドのタイムスタンプが更新されただけで値が同じなら、そのフィールドはdeltaに現れない。

- **問題1「2回目以降が無反応」**: 例えば`sendSlot: 5`を送って処理後、`reported.sendSlot`にも`5`をそのまま書き戻すと、次に同じスロット(`5`)をもう一度送ってもdesired/reportedが値として一致するためdeltaが生成されず、ESP32には何も配信されない
  - 対策: `sendSlot`は`"<slot>-<timestamp>"`という文字列にして毎回値そのものを変える(`handleSceneActivate`/`handlePowerController`)。AC操作(`power`/`mode`/`temp`/`fan`/`swing`)は値自体を変えると意味が壊れるため、代わりに常に変化する`cmdId`(タイムスタンプ文字列)を追加フィールドとして混ぜ、ESP32側は`cmdId`の有無もAC処理のトリガー条件に含める
- **問題2「無限に送り続ける」(実機で照明・エアコンが誤動作し続ける事故が発生)**:
  - reportedを`null`のままにする(書き戻さない)と、desiredと永久に不一致のままになり、ポーリングのたびに再送信され続ける
  - 複数フィールドを**2回に分けて**publishすると(例: `power`等を1回目のpublishで反映し、`cmdId`を2回目のpublishで反映)、1回目の直後(`cmdId`だけまだ古い)の中間状態をAWS側が観測してしまい、「一部だけ一致・一部だけ不一致」の新しいdeltaを生成してESP32へ再pushするレースコンディションで無限ループする
  - 対策: 処理した値は必ず**そのままreportedへ書き戻し**desired==reportedにする。複数フィールドを更新する場合は**1回のUpdateThingShadowCommand(1回のpublish)にまとめる**(AWS側でアトミックに処理されるため中間状態が外部から観測されない)
- **型の一致も必須**: `cmdId`をLambda側で文字列(`String(Date.now())`)として送るなら、ESP32側のreportedへの書き戻しでも同じ文字列型(ダブルクォート付き)で書く必要がある。型が食い違うと(片方が数値、片方が文字列)値として常に不一致とみなされ、問題2と同じ無限ループになる
- **push配信の取りこぼし対策**: `WiFiClientSecure`+`PubSubClient`の組み合わせでは、TLSレコードが分割されて届いた際に受信を取りこぼすことが実機で確認された(`shadow/update/delta`のリアルタイムpushが届かない)。そのため`AwsIotClient::loop()`で5秒おきに`shadow/get`を再要求するポーリングをフォールバックとして常時併用している

### 既知の制約(Alexa連携)

- 上記の通りBLEとAlexa操作は同時に使えない(排他)
- shadow deltaは変更フィールドのみ届く。ESP32側は未指定フィールドを現在値で埋めてから適用する(`IrController::getAcWireState()`)。ここを間違えると「温度だけ変えたら電源が切れる」といった事故になる
- Alexa標準の`ThermostatController`は除湿(DRY)・送風(FAN)モードを持たないため、音声では冷房/暖房/自動/オフのみ操作可能(除湿等は`web/remote.html`側のみで操作)
- 照明は1ボタントグル式のリモコンを学習させている前提。「つけて」「けして」どちらの発話でも同じ信号を送るだけなので、他の方法(壁スイッチ・付属リモコン本体等)で操作された場合はAlexa側が認識する状態と実際の点灯/消灯がズレる。ON/OFFボタンが分かれているリモコンなら2スロットに学習させ、Lambda側の`slot-n`対応付けをON用/OFF用に分けて`TurnOn`/`TurnOff`をそれぞれ別スロットに送るよう改修すれば状態ズレを解消できる
- TLS証明書の有効期限検証にはNTP時刻同期が必須(ESP32はRTC電池が無くリセットで1970年に戻るため)。`AwsIotClient::ensureTimeSynced()`がWiFi接続後に自動でNTP同期してから接続を開始する
- Cognitoユーザーは個人利用前提の単一アカウント。複数人で使う場合はユーザー追加が必要

## クラウドモード中のWeb操作(`web/remote-cloud.html`)

`web/remote.html`はBLE直結なので、クラウードモード中(BLEスタック停止中)は使えない。外出先などBLEが届かない場所からも操作したい場合は、こちらを使う。

### 構成

```
ブラウザ(remote-cloud.html) → Lambda Function URL(aws/lambda-web/index.js) → AWS IoT Shadow更新
                                                                              → ESP32(クラウードモード中)が既存の仕組みでdeltaを処理
```

ESP32側の変更は不要(既存のクラウードモードの仕組みをそのまま使う)。BLE版で行う「学習」「ラベル編集」「ACブランド変更・型番指定」「WiFi設定」はここでは非対応(shadowにその情報が無い、またはBLE経由でしか設定できないため)。送信専用リモコンとして、既存スロットの送信とAC(電源/温度/モード/送風/スイング)操作のみ行える。

### Lambda(`aws/lambda-web/index.js`)

`aws/lambda/index.js`(Alexa Smart Homeスキル用)とは別の、Web UI専用の薄いAPI。デプロイはTokyo(ap-northeast-1)、IAM Roleは`ir-remote-alexa-skill-lambda-role`を共用(shadow read/write権限のみ)。Function URL(認証タイプ`NONE`)で公開し、認証はLambda内部で`password`フィールドの一致チェックのみ行う簡易方式。

| action | 用途 |
|---|---|
| `get_state` | reportedからスロット一覧・AC状態を取得 |
| `send_slot` | `{slot: n}` → shadow desiredに`{sendSlot: "<n>-<timestamp>"}`を書き込み |
| `set_ac` | `{power?, mode?, temp?, fan?, swing?}` → shadow desiredに`{...,cmdId: "<timestamp>"}`を書き込み |

デプロイ:
```
cd aws/lambda-web
npm install --production
zip -r function.zip index.js node_modules package.json
aws lambda update-function-code --function-name ir-remote-web-api --zip-file fileb://function.zip --region ap-northeast-1
```

**CORS注意点**: Lambda Function URL自体のCORS設定(`AllowOrigins`等)がレスポンスへCORSヘッダーを自動付与するため、Lambda関数コード側で`Access-Control-Allow-Origin`等を重複して返すと値が2つ入った不正なヘッダーになりブラウザに拒否される。CORSヘッダーはFunction URL側の設定に一本化している。

### `web/remote-cloud.html`のホスティング

S3(非公開、CloudFront経由のみ読み取り許可)+CloudFront(Origin Access Control、デフォルトドメインでHTTPS)で公開。

```
aws s3 cp web/remote-cloud.html s3://ir-remote-web-453393474681/remote-cloud.html --region ap-northeast-1 --content-type "text/html; charset=utf-8"
aws cloudfront create-invalidation --distribution-id E1Y5T1F0L162IL --paths "/remote-cloud.html"
```

初回アクセス時にAPIパスワード(Lambda環境変数`WEB_API_PASSWORD`と同じ値)を入力すると、その端末のブラウザの`localStorage`に保存され次回以降は自動接続する。5秒おきにポーリングして状態を更新するため、Alexa等別経路での操作もある程度反映される。

## 回路図

EasyEDA Proで回路図を作成済み（プロジェクト名: `ir_remote_learn`）。
ESP32-DEVKITC-VIE・SGN119はライブラリに正確な型番が無いため、ピン配置・電気特性が同等の代替部品（ESP32-DevKitC汎用シンボル／IR323/H0-A）で作図している。

## 制限事項

- IRremoteESP8266が対応していないプロトコル(家電メーカー独自のraw波形のみの信号など)は「未知プロトコル」判定となるが、その場合は受信した生パルス列をraw波形としてそのまま学習スロットに保存し、送信時は`IRsend::sendRaw()`で再生する(`IrController::storeLastDecodeToSlot()`)。1スロットあたり最大400エントリまで(超過時のみ学習失敗)
- エアコンなど長いデータ長・状態型プロトコル（Panasonic ACなど）は学習スロット側でも`state`配列を使い正しく保存・再送信できる。加えて`IRac`クラスによる汎用エアコン制御(ブランド選択+温度/モード/風量操作)も別途搭載済み（詳細は「汎用エアコン制御(IRac)」参照）
