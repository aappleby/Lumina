#pragma once
#include "LuaCallinfo.h"    // for base_ci
#include "LuaObject.h"
#include "LuaVector.h"

/* chain list of long jump buffers */
struct lua_longjmp {
  lua_longjmp *previous;
  int b;
  volatile int status;  /* error code */
};

/*
** `per thread' state
*/
class lua_State : public LuaObject {
public:
  uint8_t status;
  StkId top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci_;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  TValue* stack_last;  /* last free slot in the stack */
  LuaVector<TValue> stack;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  uint8_t hookmask;
  uint8_t allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  LuaObject *openupval;  /* list of open upvalues in this stack */
  LuaObject *gclist;
  lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */

  int stackinuse();
  void shrinkstack();
};

