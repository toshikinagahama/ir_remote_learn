#include <Arduino.h>

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
        if (irController.isLastDecodeUnknown())
        {
          enqueue(EVT_IR_LEARN_UNKNOWN);
        }
        else
        {
          irController.storeLastDecodeToSlot(g_learnTargetSlot);
          enqueue(EVT_IR_LEARN_SUCCESS);
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

  init_event_queue();

  static StateAdvertise sAdvertise;
  static StateIdle      sIdle;
  static StateLearning  sLearning;
  stateManager.registerState(&sAdvertise);
  stateManager.registerState(&sIdle);
  stateManager.registerState(&sLearning);

  ble->initialize();
  irController.begin(kRecvPin, kSendPin);

  stateManager.changeState(STATE_ADVERTISE);

  xTaskCreatePinnedToCore(irLearnTaskFunc, "IrLearnTask", 4096, NULL, 2, &hLearnTask, 1);
}

/**
 * @brief 【Main Task / loop()】イベントディスパッチ
 */
void loop()
{
  MyEvent event = dequeue(portMAX_DELAY);
  if (event.id != EVT_NOP)
  {
    stateManager.handleEvent(&event);
  }
}
