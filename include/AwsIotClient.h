#ifndef AWSIOTCLIENT_H
#define AWSIOTCLIENT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

/**
 * @brief AWS IoT Core (Device Shadow) とのMQTT/TLS接続を担当
 * shadowのdeltaを受信→既存のenqueue()イベントバスへ変換して投入する薄いアダプタ。
 * BLEと同じ「外部入力→enqueue()→既存State」の経路を通すため、State側の新規ロジックは持たない。
 * 専用FreeRTOSタスク(main.cppのawsIotTaskFunc)から継続的にloop()が呼ばれる想定。
 */
class AwsIotClient {
public:
  void begin();
  void loop();  // 接続維持・メッセージ処理。専用タスクから毎回呼ばれる

  // 現在のエアコン状態をshadowのreportedへ反映する(AcApplyState後などに呼ぶ)
  void publishAcState();
  // ラベル付きスロット一覧をshadowのreportedへ反映する(Alexa Discoveryの元データ)
  void publishSlots();

private:
  WiFiClientSecure tlsClient_;
  PubSubClient mqttClient_{tlsClient_};
  bool subscribed_ = false;

  void ensureConnected();
  void onMessage(char *topic, uint8_t *payload, unsigned int length);
  static void onMessageTrampoline(char *topic, uint8_t *payload, unsigned int length);
};

extern AwsIotClient awsIotClient;

#endif // AWSIOTCLIENT_H
