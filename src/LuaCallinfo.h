#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

/*
** information about a call
*/
class CallInfo : public LuaBase {
public:

  CallInfo() {
    func_ = NULL;
    top_ = NULL;
    previous = NULL;
    next = NULL;
    nresults = 0;
    callstatus = 0;
    base_ = NULL;
    savedpc = NULL;
    ctx = 0;
    continuation_ = NULL;
    old_errfunc = 0;
    extra = 0;
    old_allowhook = 0;
    status = 0;
  }
  ~CallInfo() {}

  const StkId getFunc() const { return func_; }
  void  setFunc(StkId func) { func_ = func; }

  const StkId getTop() const { return top_; }
  void  setTop(StkId top) { top_ = top; }

  const StkId getBase() const { return base_; }
  void  setBase(StkId base) { base_ = base; }

  CallInfo* previous;
  CallInfo* next;  /* dynamic call link */
  short nresults;  /* expected number of results from this function */
  uint8_t callstatus;

  // only for Lua functions
  const Instruction *savedpc;

  // only for C functions
  int ctx;  /* context info. in case of yields */
  lua_CFunction continuation_;  /* continuation in case of yields */

  ptrdiff_t old_errfunc;
  ptrdiff_t extra;
  uint8_t old_allowhook;
  uint8_t status;

protected:

  StkId func_;  /* function index in the stack */
  StkId	top_;  /* top for this function */
  StkId base_;  /* base for this function */
};

