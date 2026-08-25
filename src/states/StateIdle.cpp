#include "states/StateIdle.h"
#include "common/MyGlobal.h"

static void notifyAcBrandLine(const String &line)
{
  ble->notify(line.c_str());
  delay(15);  // BLEスタックの通知バッファ溢れ防止(70件超連続通知のため)
}

void StateIdle::onEnter()
{
  Serial.println("Entering STATE_IDLE");

  // 接続直後に現在の保存構成(スロット一覧・エアコン状態)をまとめてHTML側へpush
  for (uint8_t n = 0; n < kMaxSlots; n++)
  {
    ble->notify(irController.slotInfoLine(n).c_str());
  }
  ble->notify(irController.acStateLine().c_str());
}

MyState StateIdle::handleEvent(const MyEvent *event)
{
  switch (event->id)
  {
  case EVT_BLE_DISCONNECTED:
    return STATE_ADVERTISE;

  case EVT_CMD_GET_DEVICE_INFO:
    ble->notify("device_info,IR-Remote-ESP32 v1.0");
    return STATE_IDLE;

  case EVT_CMD_GET_SLOTS:
    for (uint8_t n = 0; n < kMaxSlots; n++)
    {
      ble->notify(irController.slotInfoLine(n).c_str());
    }
    return STATE_IDLE;

  case EVT_CMD_SET_LABEL:
    if (event->length >= 1)
    {
      uint8_t slot = event->payload[0];
      char buf[PAYLOAD_SIZE];
      size_t labelLen = event->length - 1;
      memcpy(buf, &event->payload[1], labelLen);
      buf[labelLen] = '\0';
      irController.setLabel(slot, String(buf));
      ble->notify(irController.slotInfoLine(slot).c_str());
    }
    return STATE_IDLE;

  case EVT_CMD_SET_CATEGORY:
    if (event->length >= 2)
    {
      uint8_t slot = event->payload[0];
      uint8_t category = event->payload[1];
      irController.setCategory(slot, category);
      ble->notify(irController.slotInfoLine(slot).c_str());
    }
    return STATE_IDLE;

  case EVT_CMD_DELETE_SLOT:
    if (event->length >= 1)
    {
      uint8_t slot = event->payload[0];
      irController.deleteSlot(slot);
      ble->notify(irController.slotInfoLine(slot).c_str());
    }
    return STATE_IDLE;

  case EVT_CMD_SEND_SLOT:
    if (event->length >= 1)
    {
      uint8_t slot = event->payload[0];
      bool ok = irController.sendSlot(slot);
      char resBuf[32];
      snprintf(resBuf, sizeof(resBuf), "send,%u,%d", slot, ok ? 1 : 0);
      ble->notify(resBuf);
    }
    return STATE_IDLE;

  case EVT_CMD_LEARN_START:
    if (event->length >= 1)
    {
      g_learnTargetSlot = event->payload[0];
      g_learnReturnState = STATE_IDLE;
      return STATE_LEARNING;
    }
    return STATE_IDLE;

  case EVT_BTN_LEARN_PRESSED:
    g_learnTargetSlot = 0;
    g_learnReturnState = STATE_IDLE;
    return STATE_LEARNING;

  case EVT_CMD_GET_AC_BRANDS:
    irController.forEachSupportedAcBrand(notifyAcBrandLine);
    return STATE_IDLE;

  case EVT_CMD_SET_AC_BRAND:
    if (event->length >= 2)
    {
      uint16_t protocolId = event->payload[0] | (event->payload[1] << 8);
      irController.acSetBrand((decode_type_t)protocolId);
      ble->notify(irController.acStateLine().c_str());
    }
    return STATE_IDLE;

  case EVT_CMD_GET_AC_STATE:
    ble->notify(irController.acStateLine().c_str());
    return STATE_IDLE;

  case EVT_CMD_SET_AC_STATE:
    if (event->length >= 5)
    {
      bool power   = event->payload[0] != 0;
      uint8_t mode = event->payload[1];
      uint8_t temp = event->payload[2];
      uint8_t fan  = event->payload[3];
      bool swing   = event->payload[4] != 0;
      irController.acApplyState(power, mode, temp, fan, swing);
      ble->notify(irController.acStateLine().c_str());
    }
    return STATE_IDLE;

  default:
    return STATE_IDLE;
  }
}
