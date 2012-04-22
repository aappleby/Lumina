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

  thread_G->isShuttingDown = true;

  // close all upvalues for this thread
  L->stack_.closeUpvals(L->stack_.begin());

  // TODO(aappleby): grayagain_ and grayhead_ still have objects in them during destruction?
  thread_G->grayhead_.Clear();
  thread_G->grayagain_.Clear();
  thread_G->weak_.Clear();
  thread_G->ephemeron_.Clear();
  thread_G->allweak_.Clear();

  luaC_freeallobjects();  /* collect all objects */

  delete thread_G->strings_;
  thread_G->strings_ = NULL;

  thread_G->buff.buffer.clear();

  assert(L->stack_.open_upvals_ == NULL);
  delete L;
  thread_L = NULL;

  assert(thread_G->getTotalBytes() == sizeof(global_State));
  delete thread_G;
  thread_G = NULL;
}


lua_State *lua_newthread (lua_State *L) {
  THREAD_CHECK(L);

  lua_State* L1 = NULL;
  {
    ScopedMemChecker c;
    L1 = new lua_State(thread_G);
    L1->linkGC(getGlobalGCHead());
    L->stack_.push(TValue(L1));
  }

  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  L1->hookcount = L1->basehookcount;
  
  {
    THREAD_CHANGE(L1);
    ScopedMemChecker c;
    L1->stack_.init();  /* init stack */
  }

  return L1;
}


lua_State *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  
  l_memcontrol.disableLimit();

  global_State* g = new global_State();
  thread_G = g;

  lua_State* L = new lua_State(g);
  thread_L = L;

  l_memcontrol.enableLimit();

  if(!l_memcontrol.limitDisabled && l_memcontrol.isOverLimit()) {
    thread_G->isShuttingDown = true;

    delete L;
    delete g;
    return NULL;
  }

  {
    GLOBAL_CHANGE(L);
    L->next_ = NULL;

    try {
      THREAD_CHECK(L);

      {
        ScopedMemChecker c;
        L->stack_.init();  /* init stack */

        g->init(L);
      }

      g->gcrunning = 1;  /* allow gc */
    }
    catch(...) {
      close_state(L);
      L = NULL;
    }
  }
  return L;
}


void lua_close (lua_State *L) {
  THREAD_CHECK(L);
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


