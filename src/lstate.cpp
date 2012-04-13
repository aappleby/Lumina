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
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
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


CallInfo *luaE_extendCI (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = new CallInfo();
  if(ci == NULL) luaD_throw(LUA_ERRMEM);
  assert(L->ci_->next == NULL);
  L->ci_->next = ci;
  ci->previous = L->ci_;
  ci->next = NULL;
  return ci;
}


/*
** Create registry table and its predefined values
*/
static void init_registry (lua_State *L, global_State *g) {
  THREAD_CHECK(L);
  TValue mt;
  
  /* create registry */
  Table *registry = new Table();
  if(registry == NULL) luaD_throw(LUA_ERRMEM);
  registry->linkGC(getGlobalGCHead());
  g->l_registry = registry;

  registry->resize(LUA_RIDX_LAST, 0);
  l_memcontrol.checkLimit();

  /* registry[LUA_RIDX_MAINTHREAD] = L */
  mt = L;
  luaH_setint(registry, LUA_RIDX_MAINTHREAD, &mt);
  /* registry[LUA_RIDX_GLOBALS] = table of globals */
  Table* t = new Table();
  if(t == NULL) luaD_throw(LUA_ERRMEM);
  t->linkGC(getGlobalGCHead());
  mt = t;
  luaH_setint(registry, LUA_RIDX_GLOBALS, &mt);
}


/*
** open parts of the state that may cause memory-allocation errors
*/
static void f_luaopen (lua_State *L, void *) {
  THREAD_CHECK(L);
  global_State *g = thread_G;
  L->initstack();  /* init stack */
  init_registry(L, g);

  luaS_resize(MINSTRTABSIZE);  /* initial size of string table */

  luaT_init();
  luaX_init(L);
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(MEMERRMSG);
  g->memerrmsg->setFixed();  /* it should never be collected */
  g->gcrunning = 1;  /* allow gc */

  l_memcontrol.checkLimit();
}


/*
** preinitialize a state with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_state (lua_State *L, global_State *g) {
  //THREAD_CHECK(L);
  L->l_G = g;
  L->ci_ = NULL;
  L->errorJmp = NULL;
  L->nCcalls = 0;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  L->hookcount = L->basehookcount;
  L->openupval = NULL;
  L->nonyieldable_count_ = 1;
  L->status = LUA_OK;
  L->errfunc = 0;
}


static void close_state (lua_State *L) {
  THREAD_CHECK(L);

  thread_G->isShuttingDown = true;

  luaF_close(L->stack.begin());  /* close all upvalues for this thread */

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

  assert(L->openupval == NULL);
  delete L;
  thread_L = NULL;

  assert(thread_G->getTotalBytes() == sizeof(global_State));
  delete thread_G;
  thread_G = NULL;
}


lua_State *lua_newthread (lua_State *L) {
  THREAD_CHECK(L);

  luaC_checkGC();

  l_memcontrol.disableLimit();

  lua_State* L1 = new lua_State();
  if(L1 == NULL) luaD_throw(LUA_ERRMEM);
  L1->linkGC(getGlobalGCHead());
  L->top[0] = L1;
  L->top++;

  l_memcontrol.enableLimit();
  l_memcontrol.checkLimit();


  preinit_state(L1, thread_G);
  
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  L1->hookcount = L1->basehookcount;
  
  L1->initstack();  /* init stack */

  l_memcontrol.checkLimit();

  return L1;
}


lua_State *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  int i;
  
  lua_State* L = new lua_State();
  if(L == NULL) { return NULL; }
  global_State* g = new global_State();
  if(g == NULL) {
    delete L;
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
    preinit_state(L, g);
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

    if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {
      /* memory allocation error: free partial state */
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


