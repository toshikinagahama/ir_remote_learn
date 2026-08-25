#include "states/StateAdvertise.h"
#include "common/MyGlobal.h"

void StateAdvertise::onEnter()
{
  Serial.println("Entering STATE_ADVERTISE");
  // クラウドモード切替時、BLEDevice::deinit()に伴うEVT_BLE_DISCONNECTEDが遅れて処理され
  // ここに来ることがある。BLEスタックは既に破棄済みなのでadvertiseStart()を呼んではいけない
  if (g_cloudModeActive) return;
  ble->advertiseStart();
}

MyState StateAdvertise::handleEvent(const MyEvent *event)
{
  switch (event->id)
  {
  case EVT_BLE_CONNECTED:
    Serial.println("BLE Connected -> Transition to STATE_IDLE");
    return STATE_IDLE;

  default:
    return STATE_ADVERTISE;
  }
}
