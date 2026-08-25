#include "states/StateAdvertise.h"
#include "common/MyGlobal.h"

void StateAdvertise::onEnter()
{
  Serial.println("Entering STATE_ADVERTISE");
  ble->advertiseStart();
}

MyState StateAdvertise::handleEvent(const MyEvent *event)
{
  switch (event->id)
  {
  case EVT_BLE_CONNECTED:
    Serial.println("BLE Connected -> Transition to STATE_IDLE");
    return STATE_IDLE;

  case EVT_BTN_LEARN_PRESSED:
    // BLE未接続でも物理ボタンでの学習は独立して動作させる(スロット0固定)
    g_learnTargetSlot = 0;
    g_learnReturnState = STATE_ADVERTISE;
    return STATE_LEARNING;

  default:
    return STATE_ADVERTISE;
  }
}
