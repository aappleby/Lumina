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


#if !defined(LUAI_GCPAUSE)
#define LUAI_GCPAUSE	200  /* 200% */
#endif

#if !defined(LUAI_GCMAJOR)
#define LUAI_GCMAJOR	200  /* 200% */
#endif

#if !defined(LUAI_GCMUL)
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#endif


#define MEMERRMSG       "not enough memory"


/*
** Create registry table and its predefined values
*/
static void init_registry (lua_State *L, global_State *g) {
  THREAD_CHECK(L);
  TValue mt;
  
  /* create registry */
  //ScopedMemChecker c;

  Table* registry;

  {
    ScopedMemChecker c;
    registry = new Table();
    registry->linkGC(getGlobalGCHead());
    g->l_registry = registry;
    registry->resize(LUA_RIDX_LAST, 0);
  }

  /* registry[LUA_RIDX_MAINTHREAD] = L */
  mt = L;
  {
    ScopedMemChecker c;
    registry->set(TValue(LUA_RIDX_MAINTHREAD), mt);
  }
  luaC_barrierback(registry, mt);

  // TODO(aappleby): Are barriers needed on the registry?

  /* registry[LUA_RIDX_GLOBALS] = table of globals */
  {
    ScopedMemChecker c;
    Table* t = new Table();
    t->linkGC(getGlobalGCHead());
    mt = t;
    registry->set(TValue(LUA_RIDX_GLOBALS),mt);
  }
  luaC_barrierback(registry,mt);
}


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

  luaC_checkGC();

  lua_State* L1 = NULL;
  {
    ScopedMemChecker c;
    L1 = new lua_State();
    L1->linkGC(getGlobalGCHead());
    L->stack_.push(TValue(L1));
  }

  L1->l_G = thread_G;
  L1->hook = NULL;
  L1->hookmask = 0;
  L1->basehookcount = 0;
  L1->allowhook = 1;
  L1->hookcount = L->basehookcount;
  L1->nonyieldable_count_ = 1;
  L1->status = LUA_OK;
  L1->errfunc = 0;

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
  int i;
  
  l_memcontrol.disableLimit();
  lua_State* L = new lua_State();
  if(L == NULL) { return NULL; }
  global_State* g = new global_State();
  if(g == NULL) {
    delete L;
    return NULL;
  }
  l_memcontrol.enableLimit();
  if(!l_memcontrol.limitDisabled && l_memcontrol.isOverLimit()) {
    delete L;
    delete g;
    return NULL;
  }

  L->l_G = g;
  {
    GLOBAL_CHANGE(L);
    L->next_ = NULL;
    //L->tt = LUA_TTHREAD;
    assert(L->type() == LUA_TTHREAD);
    g->livecolor = LuaObject::colorA;
    g->deadcolor = LuaObject::colorB;
    L->makeLive();
    g->gckind = KGC_NORMAL;
    
    L->l_G = g;
    L->hook = NULL;
    L->hookmask = 0;
    L->basehookcount = 0;
    L->allowhook = 1;
    L->hookcount = L->basehookcount;
    L->nonyieldable_count_ = 1;
    L->status = LUA_OK;
    L->errfunc = 0;
    
    g->mainthread = L;
    g->uvhead.uprev = &g->uvhead;
    g->uvhead.unext = &g->uvhead;
    g->gcrunning = 0;  /* no GC while building state */
    g->lastmajormem = 0;
    g->strings_ = new stringtable();
    g->panic = NULL;
    g->version = lua_version(NULL);
    g->gcstate = GCSpause;
    g->allgc = NULL;
    g->finobj = NULL;
    //g->tobefnz = NULL;

    g->gcpause = LUAI_GCPAUSE;
    g->gcmajorinc = LUAI_GCMAJOR;
    g->gcstepmul = LUAI_GCMUL;
    for (i=0; i < LUA_NUMTAGS; i++) g->base_metatables_[i] = NULL;

    try {
      THREAD_CHECK(L);

      global_State *g = thread_G;

      {
        ScopedMemChecker c;
        L->stack_.init();  /* init stack */
      }

      init_registry(L, g);

      {
        ScopedMemChecker c;
        g->strings_->resize(MINSTRTABSIZE);  /* initial size of string table */
      }

      luaT_init();
      luaX_init(L);

      /* pre-create memory-error message */
      {
        ScopedMemChecker c;
        g->memerrmsg = thread_G->strings_->Create(MEMERRMSG);
        g->memerrmsg->setFixed();  /* it should never be collected */
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


