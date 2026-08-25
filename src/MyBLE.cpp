#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "MyBLE.h"
#include "common/MyGlobal.h"

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    enqueue(EVT_BLE_CONNECTED);
    Serial.println("BLE Connected");
  };

  void onDisconnect(BLEServer *pServer)
  {
    pServer->startAdvertising();
    enqueue(EVT_BLE_DISCONNECTED);
    Serial.println("BLE Disconnected");
  }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string data = pCharacteristic->getValue();
    if (data.length() < 2) return;

    // カテゴリ (data[0]) と種別 (data[1]) を 16bit 合成コードに変換
    uint16_t cmd = ((uint8_t)data[0] << 8) | (uint8_t)data[1];
    std::string payload = (data.length() > 2) ? data.substr(2) : "";

    switch (cmd)
    {
    case BLECmd::GET_DEVICE_INFO: enqueue(EVT_CMD_GET_DEVICE_INFO); break;
    case BLECmd::GET_SLOTS:       enqueue(EVT_CMD_GET_SLOTS); break;
    case BLECmd::SET_LABEL:       if (!payload.empty()) enqueue(EVT_CMD_SET_LABEL, payload.data(), payload.length()); break;
    case BLECmd::DELETE_SLOT:     if (!payload.empty()) enqueue(EVT_CMD_DELETE_SLOT, payload.data(), payload.length()); break;
    case BLECmd::SET_CATEGORY:    if (!payload.empty()) enqueue(EVT_CMD_SET_CATEGORY, payload.data(), payload.length()); break;
    case BLECmd::LEARN_START:     if (!payload.empty()) enqueue(EVT_CMD_LEARN_START, payload.data(), payload.length()); break;
    case BLECmd::LEARN_CANCEL:    enqueue(EVT_CMD_LEARN_CANCEL); break;
    case BLECmd::SEND_SLOT:       if (!payload.empty()) enqueue(EVT_CMD_SEND_SLOT, payload.data(), payload.length()); break;
    case BLECmd::GET_AC_BRANDS:   enqueue(EVT_CMD_GET_AC_BRANDS); break;
    case BLECmd::SET_AC_BRAND:    if (!payload.empty()) enqueue(EVT_CMD_SET_AC_BRAND, payload.data(), payload.length()); break;
    case BLECmd::GET_AC_STATE:    enqueue(EVT_CMD_GET_AC_STATE); break;
    case BLECmd::SET_AC_STATE:    if (!payload.empty()) enqueue(EVT_CMD_SET_AC_STATE, payload.data(), payload.length()); break;
    case BLECmd::SET_AC_MODEL:    if (!payload.empty()) enqueue(EVT_CMD_SET_AC_MODEL, payload.data(), payload.length()); break;
    case BLECmd::SET_WIFI_SSID:   if (!payload.empty()) enqueue(EVT_CMD_SET_WIFI_SSID, payload.data(), payload.length()); break;
    case BLECmd::SET_WIFI_PW:     if (!payload.empty()) enqueue(EVT_CMD_SET_WIFI_PW, payload.data(), payload.length()); break;
    case BLECmd::GET_WIFI_STATUS: enqueue(EVT_CMD_GET_WIFI_STATUS); break;
    case BLECmd::CONNECT_WIFI:    enqueue(EVT_CMD_CONNECT_WIFI); break;
    case BLECmd::ENABLE_CLOUD_MODE: enqueue(EVT_CMD_ENABLE_CLOUD_MODE); break;
    default:
      Serial.printf("Unknown BLE Cmd: 0x%04X\n", cmd);
      break;
    }
  }
};

static MyServerCallbacks serverCallbacks;
static MyCallbacks charCallbacks;
static BLE2902 descriptor2902;

void MyBLE::initialize()
{
  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic->setCallbacks(&charCallbacks);
  pCharacteristic->addDescriptor(&descriptor2902);

  pAdvertising = pServer->getAdvertising();
}

void MyBLE::advertiseStart()
{
  pService->start();
  pAdvertising->start();
}

void MyBLE::advertiseStop()
{
  pService->stop();
  pAdvertising->stop();
}

void MyBLE::notify(const char *val)
{
  pCharacteristic->setValue((uint8_t *)val, strlen(val));
  pCharacteristic->notify();
}

void MyBLE::notify(const std::string &val)
{
  pCharacteristic->setValue((uint8_t *)val.data(), val.length());
  pCharacteristic->notify();
}

void MyBLE::notify(uint8_t *val, size_t len)
{
  pCharacteristic->setValue(val, len);
  pCharacteristic->notify();
}
