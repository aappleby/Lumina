/*
** $Id: lstate.c,v 2.92 2011/10/03 17:54:25 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#include "LuaCallinfo.h"
#include "LuaGlobals.h"
#include "LuaState.h"

#include <stddef.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "ltm.h"


lua_State *lua_newthread (lua_State *L) {
  THREAD_CHECK(L);
  ScopedMemChecker c;

  lua_State* L1 = new lua_State(L);
  L->stack_.push(TValue(L1));

  return L1;
}


lua_State *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  
  l_memcontrol.disableLimit();

  global_State* g = new global_State();

  l_memcontrol.enableLimit();

  // TODO(aappleby): Need to investigate why removing this check and letting the
  // checkLimit in luaV_execute handle it doesn't work.
  if(l_memcontrol.isOverLimit()) {
    delete g;
    return NULL;
  }

  return g->mainthread;
}


void lua_close (lua_State *L) {
  THREAD_CHECK(L);
  global_State* g = L->l_G;
  delete g;
}


