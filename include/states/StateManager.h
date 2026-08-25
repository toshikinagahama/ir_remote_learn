#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include "states/State.h"
#include <map>

/**
 * @brief 状態管理・自動エントリー/エグジットクラス
 */
class StateManager
{
private:
  State *currentState = nullptr;
  std::map<MyState, State *> stateMap;

public:
  ~StateManager();

  void registerState(State *state);
  void changeState(MyState newStateID);
  void handleEvent(const MyEvent *event);
  MyState getCurrentStateID() const;
};

#endif // STATEMANAGER_H
