#ifndef STATEIDLE_H
#define STATEIDLE_H

#include "states/State.h"

class StateIdle : public State
{
public:
  MyState getStateID() const override { return STATE_IDLE; }
  void onEnter() override;
  MyState handleEvent(const MyEvent *event) override;
};

#endif // STATEIDLE_H
