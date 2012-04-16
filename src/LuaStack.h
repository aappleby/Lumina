#pragma once

#include "LuaCallinfo.h"
#include "LuaValue.h"
#include "LuaVector.h"

//------------------------------------------------------------------------------

class LuaStack : public LuaVector<TValue> {
public:

  LuaStack() {
    top_ = NULL;
    callinfo_ = &callinfo_head_;
    open_upvals_ = NULL;
  }

  ~LuaStack() {
    assert(open_upvals_ == NULL);
  }

  TValue* last() {
    return end() - EXTRA_STACK;
  }

  void init();
  void realloc(int newsize);
  void free();

  int countInUse();

  TValue* top_;
  CallInfo callinfo_head_;  /* CallInfo for first level (C calling Lua) */
  CallInfo* callinfo_;  /* call info for current function */

  LuaObject *open_upvals_;  /* list of open upvalues in this stack */
};

//------------------------------------------------------------------------------

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

  TValue& operator[] ( int offset ) {
    return stack_->begin()[index_ + offset];
  }

protected:
  LuaStack* stack_;
  int index_;
};

//------------------------------------------------------------------------------
