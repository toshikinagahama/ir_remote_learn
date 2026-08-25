#include "states/StateLearning.h"
#include "common/MyGlobal.h"

void StateLearning::onEnter()
{
  Serial.printf("Entering STATE_LEARNING (slot=%u) -> Notifying LearnTask\n", g_learnTargetSlot);
  char resBuf[16];
  snprintf(resBuf, sizeof(resBuf), "learn,start,%u", g_learnTargetSlot);
  ble->notify(resBuf);
  if (hLearnTask != NULL)
  {
    xTaskNotifyGive(hLearnTask);
  }
}

void StateLearning::onExit()
{
  Serial.println("Exiting STATE_LEARNING");
}

MyState StateLearning::handleEvent(const MyEvent *event)
{
  switch (event->id)
  {
  case EVT_CMD_LEARN_CANCEL:
    ble->notify("learn,cancelled");
    return STATE_IDLE;

  case EVT_IR_LEARN_SUCCESS:
    ble->notify(irController.slotInfoLine(g_learnTargetSlot).c_str());
    return STATE_IDLE;

  case EVT_IR_LEARN_UNKNOWN:
    ble->notify("learn,unknown");
    return STATE_IDLE;

  case EVT_IR_LEARN_TIMEOUT:
    ble->notify("learn,timeout");
    return STATE_IDLE;

  case EVT_BLE_DISCONNECTED:
    return STATE_ADVERTISE;

  default:
    return STATE_LEARNING;
  }
}
