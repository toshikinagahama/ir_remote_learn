#ifndef MYGLOBAL_H
#define MYGLOBAL_H

#include <Arduino.h>
#include "common/MyState.h"
#include "common/MyEvent.h"
#include "MyBLE.h"
#include "IrController.h"
#include "WifiManager.h"
#include "AwsIotClient.h"

extern MyBLE *ble;
extern TaskHandle_t hLearnTask;
extern uint8_t g_learnTargetSlot;   // STATE_LEARNING 遷移直前にセットする学習対象スロット
extern bool g_cloudModeActive;      // true: BLEを止めてAWS IoT接続中(排他モード、戻すには電源再投入)

// Event Queue Interface
void init_event_queue();
void enqueue(MyEventID id, const void* payload = NULL, size_t length = 0);
MyEvent dequeue(TickType_t xTicksToWait = portMAX_DELAY);

#endif // MYGLOBAL_H
