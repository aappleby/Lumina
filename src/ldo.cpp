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

void adjustresults(LuaThread* L, int nres);
void checkresults(LuaThread* L, int nargs, int nresults);

/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

static LuaValue geterrorobj (LuaThread *L, int errcode ) {
  THREAD_CHECK(L);
  if(errcode == LUA_ERRMEM) return LuaValue(thread_G->memerrmsg);
  if(errcode == LUA_ERRERR) return LuaValue(thread_G->strings_->Create("error in error handling"));
  return L->stack_.top(-1);
}

static void seterrorobj (LuaThread *L, int errcode, StkId oldtop) {
  THREAD_CHECK(L);
  LuaValue errobj = geterrorobj(L,errcode);
  L->stack_.setTop(oldtop);
  L->stack_.push_nocheck(errobj);
}

/* }====================================================== */

void luaD_hook (LuaThread *L, int event, int line) {
  THREAD_CHECK(L);
  LuaHook hook = L->hook;
  if (hook && L->allowhook) {
    LuaStackFrame *ci = L->stack_.callinfo_;

    LuaDebug ar(event, line, ci);

    // Save the stack and callinfo tops.
    ptrdiff_t top = L->stack_.indexOf(L->stack_.top_);
    ptrdiff_t ci_top = L->stack_.indexOf(ci->getTop());
    
    // Make sure the stack can hold enough values for a C call
    LuaResult result = L->stack_.reserve2(LUA_MINSTACK);
    handleResult(result);

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
    ci->setTop( L->stack_.atIndex(ci_top) );
    L->stack_.top_ = L->stack_.atIndex(top);
  }
}


static LuaResult  tryfuncTM (LuaThread *L, int funcindex) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);

  LuaValue* func = &L->stack_[funcindex];
  LuaValue tm = luaT_gettmbyobj2(*func, TM_CALL);

  if (!tm.isFunction()) {
    result = luaG_typeerror(func, "call");
    if(result != LUA_OK) return result;
  }

  int nargs = L->stack_.topsize() - funcindex - 1;

  for(int i = 0; i < nargs + 1; i++) {
    L->stack_.top_[-i] = L->stack_.top_[-i-1];
  }
  L->stack_.top_[-nargs-1] = tm;
  L->stack_.top_++;

  result = L->stack_.reserve2(0);
  return result;
}


LuaResult luaD_precall2 (LuaThread *L, int funcindex, int nresults) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);

  LuaValue func = L->stack_[funcindex];

  if(!func.isFunction()) {
    result = tryfuncTM(L, funcindex);  /* retry with 'function' tag method */
    if(result != LUA_OK) return result;
    func = L->stack_[funcindex];
    assert(func.isFunction());
  }

  int isC = (func.isCallback() || func.isCClosure()) ? 1 : 0;

  int nargs = L->stack_.topsize() - funcindex - 1;

  if(isC) {
    result = L->stack_.createCCall2(nargs, nresults);
    if(result != LUA_OK) return result;
  }
  else {
    result = L->stack_.createLuaCall(nargs, nresults);
    if(result != LUA_OK) return result;
  }

  L->stack_.callinfo_ = L->stack_.callinfo_->next;
  if(!isC) L->stack_.top_ = L->stack_.callinfo_->getTop();

  if (L->hookmask & LUA_MASKCALL) {
    LuaStackFrame* ci = L->stack_.callinfo_;

    if (ci->previous->getCurrentOp() == OP_TAILCALL) {
      ci->callstatus |= CIST_TAIL;
      luaD_hook(L, LUA_HOOKTAILCALL, -1);
    }
    else {
      luaD_hook(L, LUA_HOOKCALL, -1);
    }
  }

  return result;
}

void luaD_postcall (LuaThread *L, StkId firstResult, int /*nresults2*/) {
  THREAD_CHECK(L);

  LuaStackFrame *ci = L->stack_.callinfo_;

  if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) {
    if (L->hookmask & LUA_MASKRET) {
      ptrdiff_t fr = L->stack_.indexOf(firstResult);  /* hook may change stack */
      luaD_hook(L, LUA_HOOKRET, -1);
      firstResult = L->stack_.atIndex(fr);
    }
    L->oldpc = ci->previous->getCurrentPC();  /* 'oldpc' for caller function */
  }

  LuaValue* res = ci->getFunc();  /* res == final position of 1st result */

  /* move results to correct place */
  //int nresults = (ci->nresults == LUA_MULTRET) ? L->stack_.top_ - firstResult : ci->nresults;
  int nresults = ci->nresults;
  if(nresults == LUA_MULTRET) {
    nresults = L->stack_.top_ - firstResult;
  }

  for(int i = 0; i < nresults; i++) {
    if(firstResult < L->stack_.top_) {
      res[i] = *firstResult++;
    }
    else {
      res[i] = LuaValue::Nil();
    }
  }

  L->stack_.top_ = res + nresults;
  L->stack_.callinfo_ = ci->previous;  /* back to caller */
}


/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
void luaD_call (LuaThread *L, int nargs, int nResults, int allowyield) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");

  //L->stack_.checkArgs(nargs+1);
  //checkresults(L, nargs, nResults);

  if (!allowyield) L->nonyieldable_count_++;

  int funcindex = L->stack_.topsize() - nargs - 1;

  result = luaD_precall2(L, funcindex, nResults);
  handleResult(result);

  luaV_execute(L, funcindex, nResults);

  if (!allowyield) L->nonyieldable_count_--;

  adjustresults(L, nResults);
}

void lua_call (LuaThread *L, int nargs, int nresults) {
  THREAD_CHECK(L);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");

  L->stack_.checkArgs(nargs+1);
  checkresults(L, nargs, nresults);

  luaD_call(L, nargs, nresults, 0);
}



void lua_callk (LuaThread *L, int nargs, int nresults, int ctx, LuaCallback k) {
  THREAD_CHECK(L);
  api_check(k == NULL || !L->stack_.callinfo_->isLua(), "cannot use continuations inside hooks");
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");

  L->stack_.checkArgs(nargs+1);
  checkresults(L, nargs, nresults);

  if (k != NULL && L->nonyieldable_count_ == 0) {  /* need to prepare continuation? */
    L->stack_.callinfo_->continuation_ = k;  /* save continuation */
    L->stack_.callinfo_->continuation_context_ = ctx;  /* save context */
    luaD_call(L, nargs, nresults, 1);  /* do the call */
  }
  else {
    /* no continuation or no yieldable */
    luaD_call(L, nargs, nresults, 0);  /* just do the call */
  }
}






static void finishCcall (LuaThread *L) {
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;
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
  luaD_postcall(L, L->stack_.top_ - n, n);
}


static void unroll (LuaThread *L, void *ud) {
  THREAD_CHECK(L);
  UNUSED(ud);
  for (int depth = 0;; depth++) {
    if (L->stack_.callinfoEmpty()) {
      /* stack is empty? */
      return;  /* coroutine finished normally */
    }

    if (!L->stack_.callinfo_->isLua()) {  /* C function? */
      finishCcall(L);
    }
    else {  /* Lua function */
      luaV_finishOp(L);  /* finish interrupted instruction */
      luaV_run(L);  /* execute down to higher C 'boundary' */
    }
  }
}


static int recover (LuaThread *L, int status) {
  THREAD_CHECK(L);
  StkId oldtop;
  LuaStackFrame *ci = L->stack_.findProtectedCall();
  if (ci == NULL) return 0;  /* no recovery point */
  /* "finish" luaD_pcall */
  oldtop = L->stack_.atIndex(ci->old_func_);
  L->stack_.closeUpvals(oldtop);
  seterrorobj(L, status, oldtop);
  L->stack_.callinfo_ = ci;
  L->allowhook = ci->old_allowhook;
  L->nonyieldable_count_ = 0;  /* should be zero to be yieldable */

  L->stack_.shrink();

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
static l_noret resume_error (LuaThread *L, const char *msg, StkId firstArg) {
  THREAD_CHECK(L);

  L->stack_.top_ = firstArg;  /* remove args from the stack */

  /* push error message */
  LuaString* s = thread_G->strings_->Create(msg);
  LuaResult result = L->stack_.push_reserve2(LuaValue(s));
  handleResult(result);

  throwError((LuaResult)-1);  /* jump back to 'lua_resume' */
}


/*
** do the work for 'lua_resume' in protected mode
*/
static void resume_coroutine (LuaThread *L, int nargs) {
  LuaResult result = LUA_OK;

  THREAD_CHECK(L);

  StkId firstArg = L->stack_.top_ - nargs;

  if (L->status == LUA_OK) {  /* may be starting a coroutine */
    if (L->stack_.callinfo_ != L->stack_.callinfo_head_) {
      /* not in base level? */
      resume_error(L, "cannot resume non-suspended coroutine", firstArg);
    }

    /* coroutine is in base level; start running it */
    int funcindex = L->stack_.topsize() - nargs - 1;

    result = luaD_precall2(L, funcindex, LUA_MULTRET);
    handleResult(result);
    
    luaV_execute(L, funcindex, LUA_MULTRET);

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
    luaV_run(L);  /* just continue running Lua code */
    unroll(L, NULL);
    return;
  }

  // 'common' yield
  L->stack_.callinfo_->setFunc(L->stack_.atIndex(L->stack_.callinfo_->old_func_));

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

  if(L->stack_.callinfo_->nresults == -1) {
    int b = 0;
    b++;
  }

  luaD_postcall(L, firstArg, -1000000000);

  unroll(L, NULL);
}


int lua_resume (LuaThread *L, LuaThread * /*from*/, int nargs) {
  THREAD_CHECK(L);
  L->nonyieldable_count_ = 0;  /* allow yields */
  L->stack_.checkArgs((L->status == LUA_OK) ? nargs + 1 : nargs);

  int status = LUA_OK;
  try {
    resume_coroutine(L, nargs);
  }
  catch (LuaResult error) {
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
      } catch(LuaResult error) {
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


int lua_yield (LuaThread *L, int nresults) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;
  L->stack_.checkArgs(nresults);
  if (L->nonyieldable_count_ > 0) {
    if (L != G(L)->mainthread) {
      result = luaG_runerror("attempt to yield across metamethod/C-call boundary");
      handleResult(result);
    }
    else {
      result = luaG_runerror("attempt to yield from outside a coroutine");
      handleResult(result);
    }
  }

  L->status = LUA_YIELD;

  if (!ci->isLua()) {
    ci->continuation_ = NULL;
    ci->old_func_ = L->stack_.indexOf(ci->getFunc());  /* save current 'func' */
    ci->setFunc(L->stack_.top_ - nresults - 1);  /* protect stack below results */
    throwError(LUA_YIELD);
  }
  assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  return 0;  /* return to 'luaD_hook' */
}

int lua_yieldk (LuaThread *L, int nresults, int ctx, LuaCallback k) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  L->stack_.checkArgs(nresults);

  if (L->nonyieldable_count_ > 0) {
    if (L != G(L)->mainthread) {
      result = luaG_runerror("attempt to yield across metamethod/C-call boundary");
      handleResult(result);
    }
    else {
      result = luaG_runerror("attempt to yield from outside a coroutine");
      handleResult(result);
    }
  }

  L->status = LUA_YIELD;

  if (L->stack_.callinfo_->isLua()) {  /* inside a hook? */
    api_check(k == NULL, "hooks cannot continue after yielding");
    assert(L->stack_.callinfo_->callstatus & CIST_HOOKED);  /* must be inside a hook */
    return 0;  /* return to 'luaD_hook' */
  }

  L->stack_.callinfo_->continuation_ = k;
  L->stack_.callinfo_->continuation_context_ = ctx;  /* save context */

  L->stack_.callinfo_->old_func_ = L->stack_.indexOf(L->stack_.callinfo_->getFunc());  /* save current 'func' */
  L->stack_.callinfo_->setFunc(L->stack_.top_ - nresults - 1);  /* protect stack below results */

  throwError(LUA_YIELD);
  return 0;
}

/*
** Execute a protected parser.
*/

static void checkmode (LuaThread *L, const char *mode, const char *x) {
  THREAD_CHECK(L);
  if (mode && strchr(mode, x[0]) == NULL) {
    luaO_pushfstring(L, "attempt to load a %s chunk (mode is " LUA_QS ")", x, mode);
    throwError(LUA_ERRSYNTAX);
  }
}

int luaD_protectedparser (LuaThread *L, ZIO *z, const char *name, const char *mode) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  LuaExecutionState s = L->saveState(L->stack_.top_);
  L->nonyieldable_count_++;  /* cannot yield during parsing */

  try {
    LuaProto *new_proto;

    int c = z->getc();  /* read first character */
    if (c == LUA_SIGNATURE[0]) {
      checkmode(L, mode, "binary");
      new_proto = luaU_undump(L, z, name);
    }
    else {
      checkmode(L, mode, "text");
      Dyndata dyd;
      result = luaY_parser(L, z, &dyd, name, c, new_proto);
      if(result != LUA_OK) {
        L->restoreState(s, result, 0);
        return result;
      }
    }
    
    LuaResult result = L->stack_.push_reserve2(LuaValue(new_proto));
    if(result != LUA_OK) {
      L->restoreState(s, result, 0);
      return result;
    }

    LuaClosure* cl = new LuaClosure(new_proto, (int)new_proto->upvalues.size());
    L->stack_.top_[-1] = LuaValue(cl);
    // initialize upvalues
    for (int i = 0; i < (int)new_proto->upvalues.size(); i++) {
      cl->ppupvals_[i] = new LuaUpvalue();
      cl->ppupvals_[i]->linkGC(getGlobalGCList());
    }
  }
  catch(LuaResult error) {
    result = error;
  }

  L->restoreState(s, result, 0);
  return result;
}


