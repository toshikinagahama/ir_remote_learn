#ifndef STATELEARNING_H
#define STATELEARNING_H

#include "states/State.h"

class StateLearning : public State
{
public:
  MyState getStateID() const override { return STATE_LEARNING; }
  void onEnter() override;
  void onExit() override;
  MyState handleEvent(const MyEvent *event) override;
};

#endif // STATELEARNING_H
