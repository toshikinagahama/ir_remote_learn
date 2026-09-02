#include "AwsIotClient.h"
#include <ArduinoJson.h>
#include "secrets.h"
#include "common/MyGlobal.h"

AwsIotClient awsIotClient;

static const char kTopicDelta[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/update/delta";
static const char kTopicUpdate[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/update";
static const char kTopicGet[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/get";
static const char kTopicGetAccepted[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/get/accepted";

void AwsIotClient::begin() {
  tlsClient_.setCACert(AWS_CERT_CA);
  tlsClient_.setCertificate(AWS_CERT_CRT);
  tlsClient_.setPrivateKey(AWS_CERT_PRIVATE);

  mqttClient_.setServer(AWS_IOT_ENDPOINT, 8883);
  mqttClient_.setCallback(onMessageTrampoline);
  // PubSubClientのデフォルト受信バッファ(256B)だとshadow/get/acceptedのフルドキュメント
  // (desired+reported+metadata全部)を受信できず、それ以降のdelta受信も機能しなくなるため拡張
  mqttClient_.setBufferSize(2048);
}

bool AwsIotClient::ensureTimeSynced() {
  if (timeSynced_) return true;

  time_t now = time(nullptr);
  if (now > 1700000000) {  // 2023-11以降なら同期済みとみなす(ESP32はRTC電池が無くリセットで1970に戻るため)
    timeSynced_ = true;
    return true;
  }

  static bool ntpStarted = false;
  if (!ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
    Serial.println("AWS IoT: NTP time sync started (required for TLS cert validation)");
  }
  return false;
}

void AwsIotClient::ensureConnected() {
  if (!g_cloudModeActive) return;  // BLE稼働中はメモリ確保できないため接続を試みない(排他運用)
  if (!wifiManager.isConnected()) return;
  if (mqttClient_.connected()) return;
  if (!ensureTimeSynced()) return;

  subscribed_ = false;
  Serial.printf("AWS IoT: connecting... free heap=%u, max alloc block=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (mqttClient_.connect(AWS_IOT_THING_NAME)) {
    Serial.println("AWS IoT: MQTT connected");
    if (mqttClient_.subscribe(kTopicDelta) && mqttClient_.subscribe(kTopicGetAccepted)) {
      subscribed_ = true;
      Serial.println("AWS IoT: subscribed to shadow delta/get-accepted");
      publishAcState();  // 起動直後の状態をreportedへ反映しておく
      publishSlots();

      // deltaトピックは「更新があった瞬間」しかpushされない(再購読しても過去の未消化分は
      // 自動再送されない)。オフライン中に溜まった変更を取りこぼさないよう、接続直後に
      // shadow/get で現在のフルドキュメント(delta含む)を明示的に取得する
      mqttClient_.publish(kTopicGet, "");
      Serial.println("AWS IoT: requested current shadow (shadow/get)");
    }
  }
}

void AwsIotClient::loop() {
  ensureConnected();
  if (mqttClient_.connected()) {
    mqttClient_.loop();

    // shadow/update/deltaのリアルタイムpushはWiFiClientSecure+PubSubClientの組み合わせで
    // 稀に受信を取りこぼす(TLSレコード分割時にavailable()が検知し損ねる既知の問題)ため、
    // push頼みにせず5秒おきにshadow/getを再要求してポーリングでも確実に拾えるようにする
    static uint32_t lastPoll = 0;
    if (millis() - lastPoll > 5000) {
      lastPoll = millis();
      mqttClient_.publish(kTopicGet, "");
    }
  }

  // デバッグ用: 接続状態を10秒おきに出力(切断/未接続に気付かず無言のまま止まる問題の切り分け用)
  static uint32_t lastStatusLog = 0;
  if (millis() - lastStatusLog > 10000) {
    lastStatusLog = millis();
    Serial.printf("AWS IoT: status wifi=%d mqttState=%d mqttConnected=%d heap=%u\n",
                  (int)wifiManager.isConnected(), mqttClient_.state(),
                  (int)mqttClient_.connected(), ESP.getFreeHeap());
  }
}

void AwsIotClient::onMessageTrampoline(char *topic, uint8_t *payload, unsigned int length) {
  awsIotClient.onMessage(topic, payload, length);
}

// delta相当のJsonObject(sendSlot / power・mode・temp・fan・swing のいずれかを含む)を処理する。
// shadow/update/delta トピックの内容、shadow/get/accepted の state.delta、両方から呼ばれる共通処理
//
// 【重要】enqueue()でBLE用のState machine(StateIdle等)に投げるのではなく、IrControllerを直接叩く。
// クラウドモード中はBLEDevice::deinit()済みでBLEスタックが存在せず、StateManagerも
// EVT_BLE_DISCONNECTEDでSTATE_ADVERTISEに戻ってしまっている(このStateはAC/送信コマンドを処理しない)。
// また万一StateIdle経由で処理できたとしても、そのハンドラは無条件でble->notify()を呼ぶため、
// BLEスタック破棄後に呼ぶとクラッシュしうる。よってクラウド系コマンドはBLE経由の経路を一切通さない。
void AwsIotClient::processDelta(JsonObject state) {
  if (state.isNull() || state.size() == 0) return;

  if (state.containsKey("sendSlot")) {
    // Lambda側は"<slot>-<timestamp>"形式の文字列で送ってくる。値そのものが毎回変わるので、
    // AWS IoT Shadowのdelta計算(値が実際に変化したフィールドのみを含む)で確実にsendSlotが
    // deltaに現れる。素の数値のままだと、同じslot番号を連続送信した時にsendSlotの値が
    // 変化しないためdeltaに含まれず、2回目以降ESP32に全く届かなかった(実際に起きた不具合)
    String raw = state["sendSlot"].as<String>();
    int dashIdx = raw.indexOf('-');
    uint8_t slot = (uint8_t)((dashIdx > 0) ? raw.substring(0, dashIdx).toInt() : raw.toInt());
    bool ok = irController.sendSlot(slot);
    Serial.printf("AWS IoT: delta sendSlot=%s (slot=%u) -> %s\n", raw.c_str(), slot, ok ? "OK" : "failed");
    // reportedへ処理した値をそのまま書き戻し、desired==reportedにしてdeltaを解消する。
    // 【注意】以前reportedをnullのままにする方式を試したが、それだとdesiredとreportedが
    // 永久に不一致のままになり、5秒おきのポーリングのたびに再送信され続ける無限ループになった
    // (実機で照明が誤動作し続ける事故が発生済み)。desired値をそのまま書き戻して一致させることが必須
    if (mqttClient_.connected()) {
      String body = "{\"state\":{\"reported\":{\"sendSlot\":\"" + raw + "\"}}}";
      mqttClient_.publish(kTopicUpdate, body.c_str());
    }
  }

  bool hasAcField = state.containsKey("power") || state.containsKey("mode") ||
                     state.containsKey("temp") || state.containsKey("fan") ||
                     state.containsKey("swing");
  // cmdId(Lambda側が毎回付与するタイムスタンプ)もトリガー対象に含める。
  // 「もう一度エアコンつけて」のように前回と全く同じ状態を指定した場合、power等の値自体は
  // 変化しないためdeltaにpower等のキーが現れない(sendSlotで起きたのと同じ構造の問題)。
  // cmdIdだけがdeltaに含まれるケースでも「現在の状態を再送信する」動作として処理する
  bool hasCmdId = state.containsKey("cmdId");
  if (hasAcField || hasCmdId) {
    // deltaは変更フィールドのみ含む。未指定分は現在値で埋める(欠落フィールドを誤ったデフォルトで
    // 上書きしないため。例: 温度だけ変更したのに電源がfalse扱いになる、を防ぐ)
    bool power; uint8_t mode, temp, fan; bool swing;
    irController.getAcWireState(power, mode, temp, fan, swing);

    if (state.containsKey("power")) power = state["power"].as<bool>();
    if (state.containsKey("mode"))  mode  = (uint8_t)state["mode"].as<int>();
    if (state.containsKey("temp"))  temp  = (uint8_t)state["temp"].as<int>();
    if (state.containsKey("fan"))   fan   = (uint8_t)state["fan"].as<int>();
    if (state.containsKey("swing")) swing = state["swing"].as<bool>();

    Serial.printf("AWS IoT: delta AC power=%d mode=%u temp=%u fan=%u swing=%d\n",
                  power, mode, temp, fan, swing);
    bool ok = irController.acApplyState(power, mode, temp, fan, swing);
    Serial.printf("AWS IoT: acApplyState -> %s\n", ok ? "OK" : "failed");

    // power/mode/temp/fan/swingとcmdIdを1回のpublishでまとめてreportedへ反映する。
    // 【注意】以前は publishAcState() とcmdIdのecho-backを別々の2回のpublishに分けていたが、
    // それだと1回目のpublish直後(cmdIdはまだ古いまま)の中間状態をAWS側が観測し、
    // 「power等は一致したがcmdIdはまだ不一致」という新しいdeltaを生成してESP32へ再pushして
    // しまうレースコンディションで無限ループが発生した(実機で複数回発生した重大な事故)。
    // 1回のUpdateThingShadowCommandはAWS側でアトミックに処理されるため、この中間状態が
    // 外部から観測されることがなくなる
    if (mqttClient_.connected()) {
      String acJson = irController.acShadowJson();  // "{...}"
      if (hasCmdId) {
        String cmdIdStr = state["cmdId"].as<String>();
        acJson = acJson.substring(0, acJson.length() - 1) + ",\"cmdId\":\"" + cmdIdStr + "\"}";
      }
      String body = "{\"state\":{\"reported\":" + acJson + "}}";
      mqttClient_.publish(kTopicUpdate, body.c_str());
    }
  }
}

void AwsIotClient::onMessage(char *topic, uint8_t *payload, unsigned int length) {
  Serial.printf("AWS IoT: onMessage topic=%s length=%u\n", topic, length);

  // shadow/get/acceptedはdesired+reported+metadata全部を含むフルドキュメントで384Bを超えるため
  // (実際に384BだとNoMemoryでパース失敗していた)、AwsIotTaskのスタック(16KB)に余裕を見て2048Bに拡張
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("AWS IoT: shadow message JSON parse error: %s\n", err.c_str());
    return;
  }

  String topicStr(topic);
  if (topicStr.endsWith("/get/accepted")) {
    Serial.println("AWS IoT: received shadow/get/accepted");
    JsonObject delta = doc["state"]["delta"];
    processDelta(delta);
  } else {
    // shadow/update/delta トピック: state直下がそのままdelta内容
    JsonObject state = doc["state"];
    processDelta(state);
  }
}

void AwsIotClient::publishAcState() {
  if (!mqttClient_.connected()) return;
  String body = "{\"state\":{\"reported\":";
  body += irController.acShadowJson();
  body += "}}";
  mqttClient_.publish(kTopicUpdate, body.c_str());
}

void AwsIotClient::publishSlots() {
  if (!mqttClient_.connected()) return;
  String body = "{\"state\":{\"reported\":{\"slots\":";
  body += irController.slotsShadowJson();
  body += "}}}";
  mqttClient_.publish(kTopicUpdate, body.c_str());
}
