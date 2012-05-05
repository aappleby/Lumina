#pragma once

#include "LuaBase.h"
#include "LuaDefines.h"
#include "LuaStack.h"
#include "LuaTypes.h"

class LuaStack;

/*
** information about a call
*/
class LuaStackFrame : public LuaBase {
public:

  void sanityCheck();

  bool isLua() const {
    return callstatus & CIST_LUA ? true : false;
  }

  void Jump ( int offset ) {
    savedpc += offset;
  }

  int beginInstruction();
  void undoInstruction();

  int getCurrentPC();
  int getCurrentLine();
  int getCurrentInstruction();
  int getCurrentOp();
  int getNextOp();
  int getNextInstruction();

  void resetPC();

  const StkId getFunc() const { return stack_->begin() + func_index_; }
  const StkId getTop() const  { return stack_->begin() + top_index_; }
  const StkId getBase() const { return stack_->begin() + base_index_; }

  void setFunc(StkId func) { func_index_ = int(func - stack_->begin()); }
  void setTop(StkId top)   { top_index_ = int(top - stack_->begin()); }
  void setBase(StkId base) { base_index_ = int(base - stack_->begin()); }

  LuaStackFrame* previous;
  LuaStackFrame* next;  /* dynamic call link */

  int nresults;  /* expected number of results from this function */
  int callstatus;

  // only for C functions
  LuaCallback continuation_;  /* continuation in case of yields */
  int continuation_context_;  /* context info. in case of yields */

  ptrdiff_t old_func_;
  ptrdiff_t old_errfunc;
  int old_allowhook;
  int status;

protected:

  // only for Lua functions
  const Instruction* code;
  const Instruction* savedpc;

  LuaStackFrame() {
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
  ~LuaStackFrame() {}

  friend class LuaStack;

  LuaStack* stack_;

  //StkId func_;  /* function index in the stack */
  int func_index_;

  //StkId	top_;  /* top for this function */
  int top_index_;
  
  //StkId base_;  /* base for this function */
  int base_index_;
};

