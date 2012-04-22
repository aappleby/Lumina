#pragma once

#include "LuaValue.h"
#include "LuaVector.h"

//------------------------------------------------------------------------------

class LuaStack : public LuaVector<TValue> {
public:

  LuaStack();
  ~LuaStack();

  TValue* last() {
    return end() - EXTRA_STACK;
  }

  //----------

  TValue* getTop() { return top_; }
  void    setTop(TValue* newtop) { top_ = newtop; }

  int  getTopIndex();
  void setTopIndex(int index);


  TValue at(int index);
  TValue at_frame(int index);

  void   copy(int index);
  void   copy_frame(int index);

  void   push(TValue v);
  void   push(const TValue* v);
  LuaResult push_reserve2(TValue v);
  void   push_nocheck(TValue v);

  TValue pop();
  void   pop(int count);
  void   remove(int index);

  void checkArgs(int count);

  //----------

  CallInfo* nextCallinfo();
  void sweepCallinfo();
  bool callinfoEmpty() {
    return callinfo_ == callinfo_head_;
  }

  CallInfo* findProtectedCall();

  LuaResult createCCall2(StkId func, int nresults, int nstack);

  //----------
  // Upvalue support

  UpVal* createUpvalFor(StkId level);
  void closeUpvals(StkId level);

  //----------

  void init();
  void free();
  LuaResult grow2(int size);
  void shrink();
  LuaResult reserve2(int newsize);

  //----------
  // Utilities

  void sanityCheck();

  //----------

  TValue* top_;
  CallInfo* callinfo_;  /* call info for current function */

  LuaObject *open_upvals_;  /* list of open upvalues in this stack */

  CallInfo* callinfo_head_;  /* CallInfo for first level (C calling Lua) */

protected:

  CallInfo* extendCallinfo();

  int countInUse();
  void realloc(int newsize);
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
