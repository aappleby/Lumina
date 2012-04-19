#pragma once
#include "LuaCallinfo.h"    // for base_ci
#include "LuaObject.h"
#include "LuaStack.h"

/* chain list of long jump buffers */
struct lua_longjmp {
  lua_longjmp *previous;
  volatile int status;  /* error code */
};

/*
** `per thread' state
*/
class lua_State : public LuaObject {
public:

  lua_State();
  ~lua_State();

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

  uint8_t status;
  global_State *l_G;

  const Instruction *oldpc;  /* last pc traced */

  LuaStack stack_;

  unsigned short nonyieldable_count_;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  
  uint8_t hookmask;
  uint8_t allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  
  int handler_count_;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */

  void closeUpvals(StkId level);
};

