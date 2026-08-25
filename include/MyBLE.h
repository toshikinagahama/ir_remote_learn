#ifndef MYBLE_H
#define MYBLE_H

#define SERVICE_UUID "d9a1c1a0-1b0e-4f7e-9a2b-6f9a2f6e6c11"        // サービスUUID
#define CHARACTERISTIC_UUID "d9a1c1a1-1b0e-4f7e-9a2b-6f9a2f6e6c11" // キャラクタリスティックUUID
#define DEVICE_NAME "IR-Remote-ESP32"                              // デバイス名

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

/**
 * BLEクラス
 */
class MyBLE
{
public:
  BLEServer *pServer = NULL;
  BLEService *pService = NULL;
  BLECharacteristic *pCharacteristic = NULL;
  BLEAdvertising *pAdvertising = NULL;

  void initialize();
  void advertiseStart();
  void advertiseStop();
  void notify(const char *);
  void notify(const std::string &);
  void notify(uint8_t *, size_t);
};

#endif // MYBLE_H
