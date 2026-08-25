#ifndef MYEVENT_H
#define MYEVENT_H

#include <Arduino.h>

#define PAYLOAD_SIZE 64

// BLE コマンドコード定義 (カテゴリ(1B) + 種別(1B) を 16bit に合成)
namespace BLECmd {
  constexpr uint16_t GET_DEVICE_INFO = 0x0001;

  // スロット管理系
  constexpr uint16_t GET_SLOTS       = 0x0100;  // 全スロット一覧取得
  constexpr uint16_t SET_LABEL       = 0x0101;  // payload: slot(1B) + label(UTF-8)
  constexpr uint16_t DELETE_SLOT     = 0x0102;  // payload: slot(1B)
  constexpr uint16_t SET_CATEGORY    = 0x0103;  // payload: slot(1B) + category(1B)

  // 学習系
  constexpr uint16_t LEARN_START     = 0x0200;  // payload: slot(1B)
  constexpr uint16_t LEARN_CANCEL    = 0x0201;

  // 送信系
  constexpr uint16_t SEND_SLOT       = 0x0300;  // payload: slot(1B)

  // 汎用エアコン(IRac)系
  constexpr uint16_t GET_AC_BRANDS   = 0x0400;  // 対応ブランド一覧取得
  constexpr uint16_t SET_AC_BRAND    = 0x0401;  // payload: protocol_id(2B little-endian)
  constexpr uint16_t GET_AC_STATE    = 0x0402;
  constexpr uint16_t SET_AC_STATE    = 0x0403;  // payload: power(1B) mode(1B) temp(1B) fan(1B) swing(1B)
  constexpr uint16_t SET_AC_MODEL    = 0x0404;  // payload: model(1B, プロトコル依存の型番index。-1相当は255)

  // WiFi/クラウド連携系
  constexpr uint16_t SET_WIFI_SSID   = 0x0500;  // payload: ssid(UTF-8)
  constexpr uint16_t SET_WIFI_PW     = 0x0501;  // payload: password(UTF-8)
  constexpr uint16_t GET_WIFI_STATUS = 0x0502;
  constexpr uint16_t CONNECT_WIFI    = 0x0503;
  constexpr uint16_t ENABLE_CLOUD_MODE = 0x0504;  // BLEを切ってAWS IoT(TLS)用にメモリを空ける。戻すには電源再投入が必要
}

enum MyEventID {
  EVT_NOP = 0,
  EVT_BLE_CONNECTED,
  EVT_BLE_DISCONNECTED,
  EVT_CMD_GET_DEVICE_INFO,
  EVT_CMD_GET_SLOTS,
  EVT_CMD_SET_LABEL,
  EVT_CMD_DELETE_SLOT,
  EVT_CMD_SET_CATEGORY,
  EVT_CMD_LEARN_START,
  EVT_CMD_LEARN_CANCEL,
  EVT_CMD_SEND_SLOT,
  EVT_CMD_GET_AC_BRANDS,
  EVT_CMD_SET_AC_BRAND,
  EVT_CMD_GET_AC_STATE,
  EVT_CMD_SET_AC_STATE,
  EVT_CMD_SET_AC_MODEL,
  EVT_CMD_SET_WIFI_SSID,
  EVT_CMD_SET_WIFI_PW,
  EVT_CMD_GET_WIFI_STATUS,
  EVT_CMD_CONNECT_WIFI,
  EVT_CMD_ENABLE_CLOUD_MODE,
  EVT_IR_LEARN_SUCCESS,    // 学習タスクからの内部通知
  EVT_IR_LEARN_UNKNOWN,
  EVT_IR_LEARN_TIMEOUT,
  EVT_MAX
};

typedef struct {
  MyEventID id;
  uint8_t payload[PAYLOAD_SIZE];
  size_t length;
  unsigned long timestamp;
} MyEvent;

#endif // MYEVENT_H
