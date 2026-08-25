#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "common/MyState.h"
#include "common/MyEvent.h"

/**
 * @brief C++ State Pattern 抽象基底クラス
 */
class State
{
public:
  virtual ~State() {}
  virtual MyState getStateID() const = 0;

  // 状態進入時/脱出時のライフサイクルイベント
  virtual void onEnter() {}
  virtual void onExit() {}

  // イベントハンドラ (遷移先の MyState を返す)
  virtual MyState handleEvent(const MyEvent *event) = 0;
};

#endif // STATE_H
