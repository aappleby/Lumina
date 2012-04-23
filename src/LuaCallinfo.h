#pragma once

#include "LuaBase.h"
#include "LuaDefines.h"
#include "LuaStack.h"
#include "LuaTypes.h"

class LuaStack;

/*
** information about a call
*/
class CallInfo : public LuaBase {
public:

  void sanityCheck();

  bool isLua() const {
    return callstatus & CIST_LUA ? true : false;
  }

  const StkId getFunc() const { return stack_->begin() + func_index_; }
  const StkId getTop() const  { return stack_->begin() + top_index_; }
  const StkId getBase() const { return stack_->begin() + base_index_; }

  void setFunc(StkId func) { func_index_ = func - stack_->begin(); }
  void setTop(StkId top)   { top_index_ = top - stack_->begin(); }
  void setBase(StkId base) { base_index_ = base - stack_->begin(); }

  CallInfo* previous;
  CallInfo* next;  /* dynamic call link */

  int nresults;  /* expected number of results from this function */
  int callstatus;

  // only for Lua functions
  const Instruction *savedpc;

  // only for C functions
  lua_CFunction continuation_;  /* continuation in case of yields */
  int continuation_context_;  /* context info. in case of yields */

  ptrdiff_t old_func_;
  ptrdiff_t old_errfunc;
  int old_allowhook;
  int status;

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

    continuation_ = NULL;
    continuation_context_ = 0;

    old_func_ = 0;
    old_errfunc = 0;
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

