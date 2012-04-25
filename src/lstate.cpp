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


LuaThread *lua_newthread (LuaThread *L) {
  THREAD_CHECK(L);

  LuaThread* L1 = new LuaThread(L);
  L->stack_.push(LuaValue(L1));

  return L1;
}


LuaThread *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  
  LuaVM* g = new LuaVM();

  // TODO(aappleby): Need to investigate why removing this check and letting the
  // checkLimit in luaV_execute handle it doesn't work.
  if(l_memcontrol.isOverLimit()) {
    delete g;
    return NULL;
  }

  return g->mainthread;
}


void lua_close (LuaThread *L) {
  THREAD_CHECK(L);
  LuaVM* g = L->l_G;
  delete g;
}


