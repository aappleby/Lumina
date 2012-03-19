#pragma once
#include "LuaTypes.h"

/*
** information about a call
*/
class CallInfo {
public:
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  CallInfo *previous, *next;  /* dynamic call link */
  short nresults;  /* expected number of results from this function */
  uint8_t callstatus;

  // only for Lua functions
  StkId base;  /* base for this function */
  const Instruction *savedpc;

  // only for C functions
  int ctx;  /* context info. in case of yields */
  lua_CFunction k;  /* continuation in case of yields */
  ptrdiff_t old_errfunc;
  ptrdiff_t extra;
  uint8_t old_allowhook;
  uint8_t status;
};

