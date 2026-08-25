#ifndef STATEADVERTISE_H
#define STATEADVERTISE_H

#include "states/State.h"

class StateAdvertise : public State
{
public:
  MyState getStateID() const override { return STATE_ADVERTISE; }
  void onEnter() override;
  MyState handleEvent(const MyEvent *event) override;
};

#endif // STATEADVERTISE_H
