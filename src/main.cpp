#include <Arduino.h>
#include <esp_bt.h>

#include "common/MyGlobal.h"
#include "states/StateManager.h"
#include "states/StateAdvertise.h"
#include "states/StateIdle.h"
#include "states/StateLearning.h"

// ピン配置（ESP32-DEVKITC-VIE ストラッピングピン回避）
const uint16_t kRecvPin = 27;      // VS1838B OUT
const uint16_t kSendPin = 26;      // SGN119 (トランジスタ駆動)

static StateManager stateManager;

/**
 * @brief BLEを止めてAWS IoT(TLS)接続用にメモリを空ける。
 * BLE+WiFi+TLSを同時に賄うだけのヒープが無いための排他運用。
 * 元に戻すには電源再投入(起動時は常にBLEモードがデフォルト)が必要。
 */
void enterCloudMode()
{
  Serial.println("Switching to CLOUD mode: stopping BLE to free memory for AWS IoT TLS");
  ble->notify("mode,cloud_switching");
  delay(300);  // notifyが実際に送信されるまでの猶予

  ble->advertiseStop();
  BLEDevice::deinit(true);

  g_cloudModeActive = true;
  Serial.println("BLE stopped. AWS IoT connection will proceed via awsIotTaskFunc.");
}

/**
 * @brief 【FreeRTOS Task】AWS IoT Core(Device Shadow)接続の維持 (Priority 1)
 * WiFi接続後、常時MQTT接続を維持しshadow deltaを受信し続ける
 */
void awsIotTaskFunc(void *arg)
{
  while (1)
  {
    awsIotClient.loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/**
 * @brief 【FreeRTOS Task】STATE_LEARNING中のみ動作するIR受信ポーリングタスク (Priority 2)
 * StateLearning::onEnter() の xTaskNotifyGive() で起床する
 */
void irLearnTaskFunc(void *arg)
{
  while (1)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    irController.resumeReceiver();
    uint32_t start = millis();
    bool done = false;

    while (!done && stateManager.getCurrentStateID() == STATE_LEARNING)
    {
      if (irController.pollDecode())
      {
        if (irController.storeLastDecodeToSlot(g_learnTargetSlot))
        {
          enqueue(EVT_IR_LEARN_SUCCESS);
        }
        else
        {
          enqueue(EVT_IR_LEARN_UNKNOWN);
        }
        done = true;
      }
      else if (millis() - start > kLearnTimeoutMs)
      {
        enqueue(EVT_IR_LEARN_TIMEOUT);
        done = true;
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("Booting IR-Remote-ESP32 (BLE)");

  // BLEのみ使用しClassic Bluetoothは不要なため、esp_bt_controller_init()より前に
  // Classic BT用メモリを解放しておく(WiFi+TLS(mbedTLS)がヒープ不足で
  // "SSL - Memory allocation failed"になるのを防ぐ、数十KB単位で空く)
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

  init_event_queue();

  static StateAdvertise sAdvertise;
  static StateIdle      sIdle;
  static StateLearning  sLearning;
  stateManager.registerState(&sAdvertise);
  stateManager.registerState(&sIdle);
  stateManager.registerState(&sLearning);

  ble->initialize();
  irController.begin(kRecvPin, kSendPin);
  wifiManager.begin();
  awsIotClient.begin();

  stateManager.changeState(STATE_ADVERTISE);

  xTaskCreatePinnedToCore(irLearnTaskFunc, "IrLearnTask", 4096, NULL, 2, &hLearnTask, 1);
  // mbedTLSのTLSハンドシェイク(RSA)はスタック消費が大きく、8KBだと不足して
  // スタックオーバーフロー→隣接メモリ破損(間欠的なPKパースエラー等)を起こすことがあるため増量
  xTaskCreatePinnedToCore(awsIotTaskFunc, "AwsIotTask", 16384, NULL, 1, NULL, 1);
}

/**
 * @brief 【Main Task / loop()】イベントディスパッチ
 */
void loop()
{
  MyEvent event = dequeue(portMAX_DELAY);
  if (event.id == EVT_CMD_ENABLE_CLOUD_MODE)
  {
    enterCloudMode();
    return;
  }
  if (event.id != EVT_NOP)
  {
    stateManager.handleEvent(&event);
  }
}
