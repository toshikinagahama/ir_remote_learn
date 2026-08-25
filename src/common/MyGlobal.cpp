#include "common/MyGlobal.h"

static MyBLE ble_inst;
MyBLE *ble = &ble_inst;
TaskHandle_t hLearnTask = NULL;
uint8_t g_learnTargetSlot = 0;
MyState g_learnReturnState = STATE_ADVERTISE;

static QueueHandle_t xEventQueue = NULL;
static const uint8_t DEFAULT_PAYLOAD[1] = {0};

void init_event_queue()
{
  if (xEventQueue == NULL)
  {
    xEventQueue = xQueueCreate(20, sizeof(MyEvent));
  }
}

void enqueue(MyEventID id, const void* payload, size_t length)
{
  init_event_queue();

  MyEvent event;
  event.id = id;
  if (payload == NULL || length == 0)
  {
    memcpy(event.payload, DEFAULT_PAYLOAD, 1);
    event.length = 0;
  }
  else
  {
    size_t copy_len = (length > PAYLOAD_SIZE) ? PAYLOAD_SIZE : length;
    memcpy(event.payload, payload, copy_len);
    event.length = copy_len;
  }
  event.timestamp = millis();

  // ESP32 (FreeRTOS) の割り込み文脈か判定
  if (xPortInIsrContext())
  {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendToBackFromISR(xEventQueue, &event, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
    {
      portYIELD_FROM_ISR();
    }
  }
  else
  {
    xQueueSendToBack(xEventQueue, &event, portMAX_DELAY);
  }
}

MyEvent dequeue(TickType_t xTicksToWait)
{
  init_event_queue();
  MyEvent event;

  if (xQueueReceive(xEventQueue, &event, xTicksToWait) == pdPASS)
  {
    return event;
  }

  MyEvent empty_event;
  empty_event.id = EVT_NOP;
  empty_event.length = 0;
  empty_event.timestamp = millis();
  return empty_event;
}
