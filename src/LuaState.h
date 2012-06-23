#pragma once
#include "LuaCallinfo.h"    // for base_ci
#include "LuaObject.h"
#include "LuaStack.h"

#include <vector>

struct LuaExecutionState {
  LuaStackFrame*  callinfo_;
  int allowhook;
  int nonyieldable_count_;
  ptrdiff_t  errfunc;
  ptrdiff_t  old_top;
};

/*
** `per thread' state
*/
class LuaThread : public LuaObject {
public:

  LuaThread(LuaVM* g);
  LuaThread(LuaThread* parent_thread);
  ~LuaThread();

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  LuaExecutionState saveState(StkId top);
  void restoreState(LuaExecutionState s, int status, int nresults);

  void PushErrors(const ErrorList& errors);

  int status;
  LuaVM *l_G;

  int oldpc;  /* last pc traced */

  LuaStack stack_;

  int nonyieldable_count_;  /* number of non-yieldable calls in stack */

  int hookmask;
  int allowhook;
  int basehookcount;
  int hookcount;
  LuaHook hook;
  
  ptrdiff_t errfunc;  /* current error handling function (stack index) */

  void closeUpvals(StkId level);
};

struct ScopedCallDepth
{
  ScopedCallDepth(LuaThread* state);
  ~ScopedCallDepth();

  LuaThread* state_;
};

struct ScopedIncrementer {
  ScopedIncrementer(int& count) : count_(count) {
    count_++;
  }

  ~ScopedIncrementer() {
    count_--;
  }
  int& count_;

private:
  ScopedIncrementer& operator = (const ScopedIncrementer&);
};