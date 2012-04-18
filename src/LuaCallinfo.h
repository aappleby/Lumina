#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaStack;

/*
** information about a call
*/
class CallInfo : public LuaBase {
public:

  const StkId getFunc() const;
  void  setFunc(StkId func);
  const StkId getTop() const;
  void  setTop(StkId top);
  const StkId getBase() const;
  void  setBase(StkId base);

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

  CallInfo() {
    stack_ = NULL;
    func_index_ = 0;
    top_index_ = 0;
    previous = NULL;
    next = NULL;
    nresults = 0;
    callstatus = 0;
    base_index_ = 0;
    savedpc = NULL;
    ctx = 0;
    continuation_ = NULL;
    old_errfunc = 0;
    extra = 0;
    old_allowhook = 0;
    status = 0;
  }
  ~CallInfo() {}

  friend class LuaStack;

  LuaStack* stack_;

  //StkId func_;  /* function index in the stack */
  int func_index_;

  //StkId	top_;  /* top for this function */
  int top_index_;
  
  //StkId base_;  /* base for this function */
  int base_index_;
};

