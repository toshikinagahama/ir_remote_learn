#include "AwsIotClient.h"
#include <ArduinoJson.h>
#include "secrets.h"
#include "common/MyGlobal.h"

AwsIotClient awsIotClient;

static const char kTopicDelta[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/update/delta";
static const char kTopicUpdate[] = "$aws/things/" AWS_IOT_THING_NAME "/shadow/update";

void AwsIotClient::begin() {
  tlsClient_.setCACert(AWS_CERT_CA);
  tlsClient_.setCertificate(AWS_CERT_CRT);
  tlsClient_.setPrivateKey(AWS_CERT_PRIVATE);

  mqttClient_.setServer(AWS_IOT_ENDPOINT, 8883);
  mqttClient_.setCallback(onMessageTrampoline);
}

void AwsIotClient::ensureConnected() {
  if (!wifiManager.isConnected()) return;
  if (mqttClient_.connected()) return;

  subscribed_ = false;
  if (mqttClient_.connect(AWS_IOT_THING_NAME)) {
    Serial.println("AWS IoT: MQTT connected");
    if (mqttClient_.subscribe(kTopicDelta)) {
      subscribed_ = true;
      Serial.println("AWS IoT: subscribed to shadow delta");
      publishAcState();  // 起動直後の状態をreportedへ反映しておく
      publishSlots();
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

void AwsIotClient::onMessage(char *topic, uint8_t *payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("AWS IoT: shadow delta JSON parse error: %s\n", err.c_str());
    return;
  }

  JsonObject state = doc["state"];
  if (state.isNull()) return;

  if (state.containsKey("sendSlot")) {
    uint8_t slot = (uint8_t)state["sendSlot"].as<int>();
    enqueue(EVT_CMD_SEND_SLOT, &slot, 1);
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

    uint8_t payloadBuf[5] = {power, mode, temp, fan, swing};
    enqueue(EVT_CMD_SET_AC_STATE, payloadBuf, 5);
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
