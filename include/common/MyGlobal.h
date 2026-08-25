#ifndef MYGLOBAL_H
#define MYGLOBAL_H

#include <Arduino.h>
#include "common/MyState.h"
#include "common/MyEvent.h"
#include "MyBLE.h"
#include "IrController.h"

extern MyBLE *ble;
extern TaskHandle_t hLearnTask;
extern uint8_t g_learnTargetSlot;   // STATE_LEARNING 遷移直前にセットする学習対象スロット
extern MyState g_learnReturnState;  // 学習完了/中断/タイムアウト後に戻る状態(IDLE or ADVERTISE)

// Event Queue Interface
void init_event_queue();
void enqueue(MyEventID id, const void* payload = NULL, size_t length = 0);
MyEvent dequeue(TickType_t xTicksToWait = portMAX_DELAY);

#endif // MYGLOBAL_H
