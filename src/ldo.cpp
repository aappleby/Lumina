/*
** $Id: ldo.c,v 2.102 2011/11/29 15:55:08 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define ldo_c

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"

/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

static TValue geterrorobj (lua_State *L, int errcode ) {
  THREAD_CHECK(L);
  if(errcode == LUA_ERRMEM) return TValue(thread_G->memerrmsg);
  if(errcode == LUA_ERRERR) return TValue(thread_G->strings_->Create("error in error handling"));
  return L->stack_.top_[-1];
}

static void seterrorobj (lua_State *L, int errcode, StkId oldtop) {
  THREAD_CHECK(L);
  TValue errobj = geterrorobj(L,errcode);
  L->stack_.top_ = oldtop;
  L->stack_.push_nocheck(errobj);
}

l_noret luaD_throw (int errcode) {

  // Throwing errors while we're in the middle of constructing an object
  // is forbidden, as that can break things badly in C++.
  /*
  if(l_memcontrol.limitDisabled) {
    assert(false);
    printf("xxx");
  }
  */

  throw(errcode);
}

/* }====================================================== */

void luaD_hook (lua_State *L, int event, int line) {
  THREAD_CHECK(L);
  lua_Hook hook = L->hook;
  if (hook && L->allowhook) {
    CallInfo *ci = L->stack_.callinfo_;

    lua_Debug ar(event, line, ci);

    // Save the stack and callinfo tops.
    ptrdiff_t top = savestack(L, L->stack_.top_);
    ptrdiff_t ci_top = savestack(L, ci->getTop());
    
    // Make sure the stack can hold enough values for a C call
    {
      ScopedMemChecker c;
      L->stack_.reserve(LUA_MINSTACK);
    }
    ci->setTop( L->stack_.top_ + LUA_MINSTACK );
    assert(ci->getTop() <= L->stack_.last());

    // Disable hooks, run the hook callback, reenable hooks.
    L->allowhook = 0;
    ci->callstatus |= CIST_HOOKED;

    (*hook)(L, &ar);
    assert(!L->allowhook);

    L->allowhook = 1;
    ci->callstatus &= ~CIST_HOOKED;

    // Clean up - restore the callinfo & stack tops.
    ci->setTop( restorestack(L, ci_top) );
    L->stack_.top_ = restorestack(L, top);
  }
}


static void callhook (lua_State *L, CallInfo *ci) {
  THREAD_CHECK(L);
  int hook = LUA_HOOKCALL;
  ci->savedpc++;  /* hooks assume 'pc' is already incremented */
  if (ci->previous->isLua() &&
      GET_OPCODE(*(ci->previous->savedpc - 1)) == OP_TAILCALL) {
    ci->callstatus |= CIST_TAIL;
    hook = LUA_HOOKTAILCALL;
  }
  luaD_hook(L, hook, -1);
  ci->savedpc--;  /* correct 'pc' */
}



static StkId tryfuncTM (lua_State *L, StkId func) {
  THREAD_CHECK(L);
  TValue tm = luaT_gettmbyobj2(*func, TM_CALL);

  if (!tm.isFunction())
    luaG_typeerror(func, "call");

  /* Open a hole inside the stack at `func' */
  for (StkId p = L->stack_.top_; p > func; p--) {
    p[0] = p[-1];
  }

  ptrdiff_t funcr = func - L->stack_.begin();
  L->stack_.top_++;
  {
    ScopedMemChecker c;
    L->stack_.reserve(0);
  }
  func = L->stack_.begin() + funcr; /* previous call may change stack */
  *func = tm;  /* tag method is the new function to be called */
  return func;
}


void luaD_precallLightC(lua_State* L, StkId func, int nresults) {
  lua_CFunction f = func->getLightFunction();

  {
    ScopedMemChecker c;
    L->stack_.createCCall(func, nresults, LUA_MINSTACK);
  }

  if (L->hookmask & LUA_MASKCALL) luaD_hook(L, LUA_HOOKCALL, -1);

  ScopedCallDepth d(L);
  if(L->l_G->call_depth_ == LUAI_MAXCCALLS) {
    luaG_runerror("C stack overflow");
  } else if (L->l_G->call_depth_ >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3))) {
    luaD_throw(LUA_ERRERR);
  }

  int n = (*f)(L);  /* do the actual call */

  L->stack_.checkArgs(n);

  luaD_postcall(L, L->stack_.top_ - n);
}

void luaD_precallC(lua_State* L, StkId func, int nresults) {
  lua_CFunction f = func->getCClosure()->cfunction_;

  {
    ScopedMemChecker c;
    L->stack_.createCCall(func, nresults, LUA_MINSTACK);
  }

  if (L->hookmask & LUA_MASKCALL) luaD_hook(L, LUA_HOOKCALL, -1);

  ScopedCallDepth d(L);
  if(L->l_G->call_depth_ == LUAI_MAXCCALLS) {
    luaG_runerror("C stack overflow");
  } else if (L->l_G->call_depth_ >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3))) {
    luaD_throw(LUA_ERRERR);
  }

  int n = (*f)(L);  /* do the actual call */

  L->stack_.checkArgs(n);

  luaD_postcall(L, L->stack_.top_ - n);
}

void luaD_precallLua(lua_State* L, StkId func, int nresults) {
  Proto *p = func->getLClosure()->proto_;

  ptrdiff_t funcr = savestack(L, func);
  {
    ScopedMemChecker c;
    L->stack_.reserve(p->maxstacksize);
  }
  func = restorestack(L, funcr);

  int nargs = cast_int(L->stack_.top_ - func) - 1;  /* number of real arguments */
  for (; nargs < p->numparams; nargs++) {
    L->stack_.push_nocheck(TValue::Nil());  /* complete missing arguments */
  }

  StkId base = func + 1;
  
  if(p->is_vararg) {

    for (int i=0; i < p->numparams; i++) {
      L->stack_.top_[i] = L->stack_.top_[i - nargs];
      L->stack_.top_[i - nargs].clear();
    }

    base = L->stack_.top_;
    L->stack_.top_ += p->numparams;
  }

  CallInfo* ci = NULL;

  {
    ScopedMemChecker c;
    ci = L->stack_.nextCallinfo();  /* now 'enter' new function */
    ci->nresults = nresults;
    ci->setFunc(func);
    ci->setBase(base);
    ci->setTop(base + p->maxstacksize);
    assert(ci->getTop() <= L->stack_.last());
    ci->savedpc = p->code.begin();
    ci->callstatus = CIST_LUA;
    L->stack_.top_ = ci->getTop();
  }

  if (L->hookmask & LUA_MASKCALL) callhook(L, ci);
}

/*
** returns true if function has been executed (C function)
*/
int luaD_precall (lua_State *L, StkId func, int nresults) {
  THREAD_CHECK(L);
  switch (func->type()) {
    case LUA_TLCF:
      {
        luaD_precallLightC(L,func,nresults);
        return 1;
      }
    case LUA_TCCL:
      {
        luaD_precallC(L,func,nresults);
        return 1;
      }
    case LUA_TLCL:
      {
        luaD_precallLua(L,func,nresults);
        return 0;
      }
    
    // Not a function.
    default:
      {
        func = tryfuncTM(L, func);  /* retry with 'function' tag method */
        return luaD_precall(L, func, nresults);  /* now it must be a function */
      }
  }
}


int luaD_postcall (lua_State *L, StkId firstResult) {
  THREAD_CHECK(L);
  StkId res;
  int wanted, i;
  CallInfo *ci = L->stack_.callinfo_;
  if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) {
    if (L->hookmask & LUA_MASKRET) {
      ptrdiff_t fr = savestack(L, firstResult);  /* hook may change stack */
      luaD_hook(L, LUA_HOOKRET, -1);
      firstResult = restorestack(L, fr);
    }
    L->oldpc = ci->previous->savedpc;  /* 'oldpc' for caller function */
  }
  res = ci->getFunc();  /* res == final position of 1st result */
  wanted = ci->nresults;
  L->stack_.callinfo_ = ci = ci->previous;  /* back to caller */

  /* move results to correct place */
  for (i = wanted; i != 0 && firstResult < L->stack_.top_; i--) {
    *res = *firstResult;
    res++;
    firstResult++;
  }
  while (i-- > 0) {
    *res = TValue::nil;
    res++;
  }
  L->stack_.top_ = res;
  return (wanted - LUA_MULTRET);  /* 0 iff wanted == LUA_MULTRET */
}


/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
void luaD_call (lua_State *L, StkId func, int nResults, int allowyield) {
  THREAD_CHECK(L);

  if (!allowyield) L->nonyieldable_count_++;
  if (!luaD_precall(L, func, nResults)) {
    /* is a Lua function? */
    luaV_execute(L);  /* call it */
  }
  if (!allowyield) L->nonyieldable_count_--;
  luaC_checkGC();
}


static void finishCcall (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->stack_.callinfo_;
  assert(ci->continuation_ != NULL);  /* must have a continuation */
  assert(L->nonyieldable_count_ == 0);
  /* finish 'luaD_call' */
  /* finish 'lua_callk' */
  adjustresults(L, ci->nresults);
  /* call continuation function */
  if (!(ci->callstatus & CIST_STAT)) {
    /* no call status? */
    ci->status = LUA_YIELD;  /* 'default' status */
  }
  assert(ci->status != LUA_OK);
  ci->callstatus = (ci->callstatus & ~(CIST_YPCALL | CIST_STAT)) | CIST_YIELDED;
  
  int n = (*ci->continuation_)(L);

  L->stack_.checkArgs(n);
  /* finish 'luaD_precall' */
  luaD_postcall(L, L->stack_.top_ - n);
}


static void unroll (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  UNUSED(ud);
  for (int depth = 0;; depth++) {
    if (L->stack_.callinfoEmpty())  /* stack is empty? */
      return;  /* coroutine finished normally */
    if (!L->stack_.callinfo_->isLua()) {  /* C function? */
      if(depth != 0) {
        int b = 0;
        b++;
      }
      finishCcall(L);
    }
    else {  /* Lua function */
      luaV_finishOp(L);  /* finish interrupted instruction */
      luaV_execute(L);  /* execute down to higher C 'boundary' */
    }
  }
}


static int recover (lua_State *L, int status) {
  THREAD_CHECK(L);
  StkId oldtop;
  CallInfo *ci = L->stack_.findProtectedCall();
  if (ci == NULL) return 0;  /* no recovery point */
  /* "finish" luaD_pcall */
  oldtop = restorestack(L, ci->old_func_);
  L->stack_.closeUpvals(oldtop);
  seterrorobj(L, status, oldtop);
  L->stack_.callinfo_ = ci;
  L->allowhook = ci->old_allowhook;
  L->nonyieldable_count_ = 0;  /* should be zero to be yieldable */
  {
    ScopedMemChecker c;
    L->stack_.shrink();
  }
  L->errfunc = ci->old_errfunc;
  ci->callstatus |= CIST_STAT;  /* call has error status */
  ci->status = status;  /* (here it is) */
  return 1;  /* continue running the coroutine */
}


/*
** signal an error in the call to 'resume', not in the execution of the
** coroutine itself. (Such errors should not be handled by any coroutine
** error handler and should not kill the coroutine.)
*/
static l_noret resume_error (lua_State *L, const char *msg, StkId firstArg) {
  THREAD_CHECK(L);
  {
    ScopedMemChecker c;
    L->stack_.top_ = firstArg;  /* remove args from the stack */

    /* push error message */
    TString* s = thread_G->strings_->Create(msg);
    L->stack_.push_reserve(TValue(s));
  }
  luaD_throw(-1);  /* jump back to 'lua_resume' */
}


/*
** do the work for 'lua_resume' in protected mode
*/
static void resume (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  StkId firstArg = cast(StkId, ud);

  if (L->status == LUA_OK) {  /* may be starting a coroutine */
    if (L->stack_.callinfo_ != &L->stack_.callinfo_head_) {
      /* not in base level? */
      resume_error(L, "cannot resume non-suspended coroutine", firstArg);
    }

    /* coroutine is in base level; start running it */
    // Lua function? call it.
    if (!luaD_precall(L, firstArg - 1, LUA_MULTRET)) {
      luaV_execute(L);
    }
    return;
  }

  if (L->status != LUA_YIELD) {
    resume_error(L, "cannot resume dead coroutine", firstArg);
    return;
  }

  /* resuming from previous yield */
  L->status = LUA_OK;

  if (L->stack_.callinfo_->isLua()) {
    /* yielded inside a hook? */
    luaV_execute(L);  /* just continue running Lua code */
    unroll(L, NULL);
    return;
  }

  // 'common' yield
  L->stack_.callinfo_->setFunc(restorestack(L, L->stack_.callinfo_->old_func_));
  if (L->stack_.callinfo_->continuation_ != NULL) {  /* does it have a continuation? */
    int n;
    L->stack_.callinfo_->status = LUA_YIELD;  /* 'default' status */
    L->stack_.callinfo_->callstatus |= CIST_YIELDED;
    n = (*L->stack_.callinfo_->continuation_)(L);  /* call continuation */
    L->stack_.checkArgs(n);
    firstArg = L->stack_.top_ - n;  /* yield results come from continuation */
  }
  /* finish 'luaD_call' */
  /* finish 'luaD_precall' */
  luaD_postcall(L, firstArg);  
  unroll(L, NULL);
}


int lua_resume (lua_State *L, lua_State *from, int nargs) {
  THREAD_CHECK(L);
  L->nonyieldable_count_ = 0;  /* allow yields */
  L->stack_.checkArgs((L->status == LUA_OK) ? nargs + 1 : nargs);

  int status = LUA_OK;
  try {
    resume(L, L->stack_.top_ - nargs);
  }
  catch (int error) {
    status = error;
  }
  
  // error calling 'lua_resume'?
  if (status == -1) {
    /* do not allow yields */
    L->nonyieldable_count_ = 1;  
    return LUA_ERRRUN;
  }

  /* yield or regular error */
  while (status != LUA_OK && status != LUA_YIELD) {  /* error? */
    /* recover point? */
    if (recover(L, status)) {
      status = LUA_OK;
      try {
        unroll(L, NULL);
      } catch(int error) {
        status = error;
      }
    }
    else {  /* unrecoverable error */
      L->status = cast_byte(status);  /* mark thread as `dead' */
      seterrorobj(L, status, L->stack_.top_);
      L->stack_.callinfo_->setTop(L->stack_.top_);
      break;
    }
  }

  assert(status == L->status);
  L->nonyieldable_count_ = 1;  /* do not allow yields */
  return status;
}


int lua_yield (lua_State *L, int nresults) {
  THREAD_CHECK(L);
  CallInfo *ci = L->stack_.callinfo_;
  L->stack_.checkArgs(nresults);
  if (L->nonyieldable_count_ > 0) {
    if (L != G(L)->mainthread)
      luaG_runerror("attempt to yield across metamethod/C-call boundary");
    else
      luaG_runerror("attempt to yield from outside a coroutine");
  }

  L->status = LUA_YIELD;

  if (!ci->isLua()) {
    ci->continuation_ = NULL;
    ci->old_func_ = savestack(L, ci->getFunc());  /* save current 'func' */
    ci->setFunc(L->stack_.top_ - nresults - 1);  /* protect stack below results */
    luaD_throw(LUA_YIELD);
  }
  assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  return 0;  /* return to 'luaD_hook' */
}

int lua_yieldk (lua_State *L, int nresults, int ctx, lua_CFunction k) {
  THREAD_CHECK(L);
  L->stack_.checkArgs(nresults);

  if (L->nonyieldable_count_ > 0) {
    if (L != G(L)->mainthread)
      luaG_runerror("attempt to yield across metamethod/C-call boundary");
    else
      luaG_runerror("attempt to yield from outside a coroutine");
  }

  L->status = LUA_YIELD;

  if (L->stack_.callinfo_->isLua()) {  /* inside a hook? */
    api_check(k == NULL, "hooks cannot continue after yielding");
    assert(L->stack_.callinfo_->callstatus & CIST_HOOKED);  /* must be inside a hook */
    return 0;  /* return to 'luaD_hook' */
  }

  L->stack_.callinfo_->continuation_ = k;
  L->stack_.callinfo_->continuation_context_ = ctx;  /* save context */

  L->stack_.callinfo_->old_func_ = savestack(L, L->stack_.callinfo_->getFunc());  /* save current 'func' */
  L->stack_.callinfo_->setFunc(L->stack_.top_ - nresults - 1);  /* protect stack below results */

  luaD_throw(LUA_YIELD);
  return 0;
}

/*
** Execute a protected parser.
*/

static void checkmode (lua_State *L, const char *mode, const char *x) {
  THREAD_CHECK(L);
  if (mode && strchr(mode, x[0]) == NULL) {
    luaO_pushfstring(L, "attempt to load a %s chunk (mode is " LUA_QS ")", x, mode);
    luaD_throw(LUA_ERRSYNTAX);
  }
}


int luaD_protectedparser (lua_State *L, ZIO *z, const char *name, const char *mode) {
  THREAD_CHECK(L);
  LuaExecutionState s = L->saveState(L->stack_.top_);
  L->nonyieldable_count_++;  /* cannot yield during parsing */

  int result = LUA_OK;
  try {
    Proto *new_proto;

    int c = zgetc(z);  /* read first character */
    if (c == LUA_SIGNATURE[0]) {
      checkmode(L, mode, "binary");
      Mbuffer buff;
      new_proto = luaU_undump(L, z, &buff, name);
    }
    else {
      checkmode(L, mode, "text");
      Mbuffer buff;
      Dyndata dyd;
      new_proto = luaY_parser(L, z, &buff, &dyd, name, c);
    }
    
    {
      ScopedMemChecker c;
      L->stack_.push_reserve(TValue(new_proto));
    }

    {
      ScopedMemChecker c;
      Closure* cl = new Closure(new_proto, (int)new_proto->upvalues.size());
      L->stack_.top_[-1] = TValue(cl);
      // initialize upvalues
      for (int i = 0; i < (int)new_proto->upvalues.size(); i++) {
        cl->ppupvals_[i] = new UpVal(getGlobalGCHead());
      }
    }
  }
  catch(int error) {
    result = error;
  }

  L->restoreState(s, result, 0);
  return result;
}


