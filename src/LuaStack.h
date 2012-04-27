#pragma once

#include "LuaValue.h"
#include "LuaVector.h"

//------------------------------------------------------------------------------

class LuaStack : public LuaVector<LuaValue> {
public:

  LuaStack();
  ~LuaStack();

  LuaValue* last() {
    return end() - EXTRA_STACK;
  }

  //----------

  LuaValue* getTop() { return top_; }
  void    setTop(LuaValue* newtop) { top_ = newtop; }

  int  getTopIndex();
  void setTopIndex(int index);


  LuaValue at(int index);
  LuaValue at_frame(int index);

  void   copy(int index);
  void   copy_frame(int index);

  void   push(LuaValue v);
  void   push(const LuaValue* v);
  LuaResult push_reserve2(LuaValue v);
  void   push_nocheck(LuaValue v);

  LuaValue pop();
  void   pop(int count);
  void   remove(int index);

  void checkArgs(int count);

  //----------

  LuaStackFrame* nextCallinfo();
  void sweepCallinfo();
  bool callinfoEmpty() {
    return callinfo_ == callinfo_head_;
  }

  LuaStackFrame* findProtectedCall();

  LuaResult createCCall2(StkId func, int nresults, int nstack);

  //----------
  // Upvalue support

  LuaUpvalue* createUpvalFor(StkId level);
  void closeUpvals(StkId level);

  //----------

  void init();
  void free();
  void shrink();
  LuaResult reserve2(int newsize);

  //----------
  // Utilities

  void sanityCheck();

  //----------

  LuaValue* top_;
  LuaStackFrame* callinfo_;  /* call info for current function */

  LuaObject *open_upvals_;  /* list of open upvalues in this stack */

  LuaStackFrame* callinfo_head_;  /* LuaStackFrame for first level (C calling Lua) */

protected:

  LuaResult grow2(int size);
  LuaStackFrame* extendCallinfo();

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

  operator LuaValue*() {
    return stack_->begin() + index_;
  }

  LuaValue& operator[] ( int offset ) {
    return stack_->begin()[index_ + offset];
  }

protected:
  LuaStack* stack_;
  int index_;
};

//------------------------------------------------------------------------------
