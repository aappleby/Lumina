#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

/*
** information about a call
*/
class CallInfo : public LuaBase {
public:

  CallInfo() {
    func = NULL;
    top = NULL;
    previous = NULL;
    next = NULL;
    nresults = 0;
    callstatus = 0;
    base = NULL;
    savedpc = NULL;
    ctx = 0;
    continuation_ = NULL;
    old_errfunc = 0;
    extra = 0;
    old_allowhook = 0;
    status = 0;
  }
  ~CallInfo() {}

  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  CallInfo* previous;
  CallInfo* next;  /* dynamic call link */
  short nresults;  /* expected number of results from this function */
  uint8_t callstatus;

  // only for Lua functions
  StkId base;  /* base for this function */
  const Instruction *savedpc;

  // only for C functions
  int ctx;  /* context info. in case of yields */
  lua_CFunction continuation_;  /* continuation in case of yields */

  ptrdiff_t old_errfunc;
  ptrdiff_t extra;
  uint8_t old_allowhook;
  uint8_t status;
};

