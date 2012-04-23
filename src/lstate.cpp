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


static void close_state (lua_State *L) {
  THREAD_CHECK(L);

  // close all upvalues for this thread
  L->stack_.closeUpvals(L->stack_.begin());

  // TODO(aappleby): grayagain_ and grayhead_ still have objects in them during destruction?

  thread_G->grayhead_.Clear();
  thread_G->grayagain_.Clear();
  thread_G->weak_.Clear();
  thread_G->ephemeron_.Clear();
  thread_G->allweak_.Clear();

  luaC_freeallobjects();  /* collect all objects */

  assert(L->stack_.open_upvals_ == NULL);
  delete L;
  thread_L = NULL;

  delete thread_G->strings_;
  thread_G->strings_ = NULL;

  thread_G->buff.buffer.clear();

  assert(thread_G->getTotalBytes() == sizeof(global_State));
  delete thread_G;
  thread_G = NULL;
}


lua_State *lua_newthread (lua_State *L) {
  THREAD_CHECK(L);
  ScopedMemChecker c;

  lua_State* L1 = new lua_State(thread_G);
  L1->linkGC(getGlobalGCHead());

  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  L1->hookcount = L1->basehookcount;
  
  L1->stack_.init();  /* init stack */

  L->stack_.push(TValue(L1));

  return L1;
}


lua_State *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  
  l_memcontrol.disableLimit();

  global_State* g = new global_State();
  thread_G = g;

  lua_State* L = new lua_State(g);
  thread_L = L;

  L->stack_.init();  /* init stack */

  g->init(L);

  l_memcontrol.enableLimit();

  if(l_memcontrol.isOverLimit()) {

    luaC_freeallobjects();

    thread_L = NULL;
    delete L;

    delete g->strings_;
    g->strings_ = NULL;

    assert(g->getTotalBytes() == sizeof(global_State));

    delete thread_G;
    thread_G = NULL;

    return NULL;
  }

  return L;
}


void lua_close (lua_State *L) {
  THREAD_CHECK(L);
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


