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

/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant
*/
void luaE_setdebt (global_State *g, l_mem debt) {
  g->totalbytes -= (debt - g->GCdebt);
  g->GCdebt = debt;
}


CallInfo *luaE_extendCI (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = (CallInfo*)luaM_alloc(sizeof(CallInfo));
  assert(L->ci_->next == NULL);
  L->ci_->next = ci;
  ci->previous = L->ci_;
  ci->next = NULL;
  return ci;
}


void luaE_freeCI (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    luaM_free(ci);
  }
}

/*
static void stack_init (lua_State *L1) {
  // initialize stack array
  L1->stack.resize(BASIC_STACK_SIZE);
  L1->top = L1->stack.begin();
  L1->stack_last = L1->stack.end() - EXTRA_STACK;

  // initialize first ci
  CallInfo* ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = L1->top;
  L1->top++;
  ci->top = L1->top + LUA_MINSTACK;
  L1->ci_ = ci;
}


static void freestack (lua_State *L) {
  THREAD_CHECK(L);
  if (L->stack.empty())
    return;  // stack not completely built yet
  L->ci_ = &L->base_ci;  // free the entire 'ci' list
  luaE_freeCI(L);
  L->stack.clear();
}
*/

/*
** Create registry table and its predefined values
*/
static void init_registry (lua_State *L, global_State *g) {
  THREAD_CHECK(L);
  TValue mt;
  /* create registry */
  Table *registry = new Table();
  sethvalue(L, &g->l_registry, registry);
  luaH_resize(registry, LUA_RIDX_LAST, 0);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(L, &mt, L);
  luaH_setint(registry, LUA_RIDX_MAINTHREAD, &mt);
  /* registry[LUA_RIDX_GLOBALS] = table of globals */
  sethvalue(L, &mt, new Table());
  luaH_setint(registry, LUA_RIDX_GLOBALS, &mt);
}


/*
** open parts of the state that may cause memory-allocation errors
*/
static void f_luaopen (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  global_State *g = G(L);
  UNUSED(ud);
  L->initstack();  /* init stack */
  init_registry(L, g);
  luaS_resize(MINSTRTABSIZE);  /* initial size of string table */
  luaT_init();
  luaX_init(L);
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(MEMERRMSG);
  luaS_fix(g->memerrmsg);  /* it should never be collected */
  g->gcrunning = 1;  /* allow gc */
}


/*
** preinitialize a state with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_state (lua_State *L, global_State *g) {
  //THREAD_CHECK(L);
  L->l_G = g;
  L->stack.init();
  L->ci_ = NULL;
  L->errorJmp = NULL;
  L->nCcalls = 0;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  L->hookcount = L->basehookcount;
  L->openupval = NULL;
  L->nny = 1;
  L->status = LUA_OK;
  L->errfunc = 0;
}


static void close_state (lua_State *L) {
  THREAD_CHECK(L);
  global_State *g = G(L);
  luaF_close(L->stack.begin());  /* close all upvalues for this thread */
  luaC_freeallobjects();  /* collect all objects */
  luaS_freestrt();

  g->buff.buffer.clear();

  L->freestack();
  assert(gettotalbytes(g) == (sizeof(lua_State) + sizeof(global_State)));
  luaM_free(g);
  L->l_G = NULL;
  luaM_free(L);  /* free main block */
}


lua_State *lua_newthread (lua_State *L) {
  THREAD_CHECK(L);

  luaC_checkGC();

  LuaObject* o = luaC_newobj(LUA_TTHREAD, sizeof(lua_State), NULL);
  LuaObject::instanceCounts[LUA_TTHREAD]++;

  lua_State* L1 = gco2th(o);

  setthvalue(L, L->top, L1);
  api_incr_top(L);
  preinit_state(L1, thread_G);
  
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  L1->hookcount = L1->basehookcount;
  
  L1->initstack();  /* init stack */
  
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  THREAD_CHECK(L);
  {
    THREAD_CHANGE(L1);
    luaF_close(L1->stack.begin());  /* close all upvalues for this thread */
    assert(L1->openupval == NULL);
    L1->freestack();
  }
  luaM_delobject(L1);
  LuaObject::instanceCounts[LUA_TTHREAD]--;
}


lua_State *lua_newstate () {
  GLOBAL_CHANGE(NULL);
  int i;
  lua_State *L;
  global_State *g;
  L = (lua_State*)luaM_alloc(sizeof(lua_State));
  if(L == NULL) { return NULL; }
  g = (global_State*)luaM_alloc(sizeof(global_State));
  if(g == NULL) {
    luaM_free(L);
    return NULL;
  }
  L->l_G = g;
  {
    GLOBAL_CHANGE(L);
    L->next = NULL;
    L->tt = LUA_TTHREAD;
    g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
    L->marked = luaC_white(g);
    g->gckind = KGC_NORMAL;
    preinit_state(L, g);
    g->mainthread = L;
    g->uvhead.uprev = &g->uvhead;
    g->uvhead.unext = &g->uvhead;
    g->gcrunning = 0;  /* no GC while building state */
    g->lastmajormem = 0;
    luaS_initstrt();
    setnilvalue(&g->l_registry);
    g->buff.buffer.init();
    g->panic = NULL;
    g->version = lua_version(NULL);
    g->gcstate = GCSpause;
    g->allgc = NULL;
    g->finobj = NULL;
    g->tobefnz = NULL;
    g->gray = g->grayagain = NULL;
    g->weak = g->ephemeron = g->allweak = NULL;
    g->totalbytes = sizeof(lua_State) + sizeof(global_State);
    g->GCdebt = 0;
    g->gcpause = LUAI_GCPAUSE;
    g->gcmajorinc = LUAI_GCMAJOR;
    g->gcstepmul = LUAI_GCMUL;
    for (i=0; i < LUA_NUMTAGS; i++) g->mt[i] = NULL;
    {
      if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {
        /* memory allocation error: free partial state */
        close_state(L);
        L = NULL;
      }
    }
  }
  return L;
}


void lua_close (lua_State *L) {
  THREAD_CHECK(L);
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


