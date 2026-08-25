#include "states/StateManager.h"

StateManager::~StateManager()
{
  for (auto &pair : stateMap)
  {
    delete pair.second;
  }
  stateMap.clear();
}

void StateManager::registerState(State *state)
{
  if (state != nullptr)
  {
    stateMap[state->getStateID()] = state;
  }
}

void StateManager::changeState(MyState newStateID)
{
  if (currentState != nullptr && currentState->getStateID() == newStateID)
  {
    return; // 同じ状態への遷移は無視
  }

  auto it = stateMap.find(newStateID);
  if (it != stateMap.end())
  {
    if (currentState != nullptr)
    {
      currentState->onExit();
    }
    currentState = it->second;
    currentState->onEnter();
  }
}

void StateManager::handleEvent(const MyEvent *event)
{
  if (currentState != nullptr && event != nullptr)
  {
    MyState nextState = currentState->handleEvent(event);
    if (nextState != currentState->getStateID())
    {
      changeState(nextState);
    }
  }
}

MyState StateManager::getCurrentStateID() const
{
  return (currentState != nullptr) ? currentState->getStateID() : STATE_ADVERTISE;
}
