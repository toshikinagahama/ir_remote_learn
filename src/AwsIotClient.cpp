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
    uint8_t slot = (uint8_t)state["sendSlot"].as<int>();
    bool ok = irController.sendSlot(slot);
    Serial.printf("AWS IoT: delta sendSlot=%u -> %s\n", slot, ok ? "OK" : "failed");
    return;
  }

  bool hasAcField = state.containsKey("power") || state.containsKey("mode") ||
                     state.containsKey("temp") || state.containsKey("fan") ||
                     state.containsKey("swing");
  if (hasAcField) {
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
    publishAcState();  // 実行結果をreportedへ反映(shadowの信頼性を保つ)
  }
}

void AwsIotClient::onMessage(char *topic, uint8_t *payload, unsigned int length) {
  StaticJsonDocument<384> doc;
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
