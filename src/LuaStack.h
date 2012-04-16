#pragma once

#include "LuaValue.h"
#include "LuaVector.h"

class LuaStack : public LuaVector<TValue> {
public:

  LuaStack() {
    top_ = NULL;
  }

  TValue* last() {
    return end() - EXTRA_STACK;
  }

  TValue* top_;
};

class LuaHandle {
public:

  LuaHandle( LuaStack* stack, int index )
  : stack_(stack),
    index_(index)
  {
  }

  operator TValue*() {
    return stack_->begin() + index_;
  }

protected:
  LuaStack* stack_;
  int index_;
};