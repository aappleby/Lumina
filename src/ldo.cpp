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
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"

/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUAI_THROW/LUAI_TRY define how Lua does exception handling. By
** default, Lua handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
/* C++ exceptions */
#define LUAI_THROW(L,c)		throw(c)
#define LUAI_TRY(L,c,a) \
	try { a } catch(...) { if ((c)->status == 0) (c)->status = -1; }



static void seterrorobj (lua_State *L, int errcode, StkId oldtop) {
  THREAD_CHECK(L);
  switch (errcode) {
    case LUA_ERRMEM: {  // memory error?
      oldtop[0] = G(L)->memerrmsg; // reuse preregistered msg.
      break;
    }
    case LUA_ERRERR: {
      oldtop[0] = luaS_newliteral("error in error handling");
      break;
    }
    default: {
      *oldtop = L->top[-1];  // error message on current top
      break;
    }
  }
  L->top = oldtop + 1;
}


l_noret luaD_throw (int errcode) {
  lua_State* L = thread_L;
  if (L->errorJmp) {  // thread has an error handler?
    L->errorJmp->status = errcode;  // set status
    LUAI_THROW(L, L->errorJmp);  // jump to it
  }
  else {  // thread has no error handler
    L->status = cast_byte(errcode);  // mark it as dead
    if (thread_G->mainthread->errorJmp) {  // main thread has a handler?
      thread_G->mainthread->top[0] = L->top[-1];  // copy error obj.
      thread_G->mainthread->top++;
      {
        THREAD_CHANGE(G(L)->mainthread);
        luaD_throw(errcode);  /* re-throw in main thread */
      }
    }
    else {  /* no handler at all; abort */
      if (G(L)->panic) {  /* panic function? */
        G(L)->panic(L);  /* call it (last chance to jump out) */
      }
      abort();
    }
  }
}


int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {
  THREAD_CHECK(L);
  unsigned short oldnCcalls = L->nCcalls;
  lua_longjmp lj;
  lj.status = LUA_OK;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  LUAI_TRY(L, &lj,
    (*f)(L, ud);
  );
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }====================================================== */

void luaD_hook (lua_State *L, int event, int line) {
  THREAD_CHECK(L);
  lua_Hook hook = L->hook;
  if (hook && L->allowhook) {
    CallInfo *ci = L->ci_;
    ptrdiff_t top = savestack(L, L->top);
    ptrdiff_t ci_top = savestack(L, ci->top);
    lua_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    L->checkstack(LUA_MINSTACK);  /* ensure minimum stack size */
    ci->top = L->top + LUA_MINSTACK;
    assert(ci->top <= L->stack_last);
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= CIST_HOOKED;
    (*hook)(L, &ar);
    assert(!L->allowhook);
    L->allowhook = 1;
    ci->top = restorestack(L, ci_top);
    L->top = restorestack(L, top);
    ci->callstatus &= ~CIST_HOOKED;
  }
}


static void callhook (lua_State *L, CallInfo *ci) {
  THREAD_CHECK(L);
  int hook = LUA_HOOKCALL;
  ci->savedpc++;  /* hooks assume 'pc' is already incremented */
  if (isLua(ci->previous) &&
      GET_OPCODE(*(ci->previous->savedpc - 1)) == OP_TAILCALL) {
    ci->callstatus |= CIST_TAIL;
    hook = LUA_HOOKTAILCALL;
  }
  luaD_hook(L, hook, -1);
  ci->savedpc--;  /* correct 'pc' */
}


void movestack(int start, int count, int dest) {
  lua_State *L = thread_L;
  for (int i=0; i < count; i++) {
    L->top[dest + i] = L->top[start + i];
    L->top[start + i].clear();
  }
}

static StkId adjust_varargs (lua_State *L, Proto *p, int actual) {
  THREAD_CHECK(L);
  assert(actual >= p->numparams);
  
  movestack(-actual, p->numparams, 0);

  StkId oldtop = L->top;
  L->top += p->numparams;
  
  return oldtop;
}


static StkId tryfuncTM (lua_State *L, StkId func) {
  THREAD_CHECK(L);
  const TValue *tm = luaT_gettmbyobj(func, TM_CALL);

  ptrdiff_t funcr = func - L->stack.begin();
  if (!tm->isFunction())
    luaG_typeerror(func, "call");

  /* Open a hole inside the stack at `func' */
  for (StkId p = L->top; p > func; p--) {
    p[0] = p[-1];
  }

  incr_top(L);
  func = L->stack.begin() + funcr; /* previous call may change stack */
  *func = *tm;  /* tag method is the new function to be called */
  return func;
}



CallInfo* next_ci(lua_State* L) {
  if(L->ci_->next == NULL) {
    L->ci_ = luaE_extendCI(L);
  } else {
    L->ci_ = L->ci_->next;
  }
  return L->ci_;
}

int luaD_precallLightC(lua_State* L, StkId func, int nresults) {
  ptrdiff_t funcr = savestack(L, func);
  lua_CFunction f = func->getLightFunction();
  L->checkstack(LUA_MINSTACK);  /* ensure minimum stack size */
  CallInfo* ci = next_ci(L);  /* now 'enter' new function */
  ci->nresults = nresults;
  ci->func = restorestack(L, funcr);
  ci->top = L->top + LUA_MINSTACK;
  assert(ci->top <= L->stack_last);
  ci->callstatus = 0;
  if (L->hookmask & LUA_MASKCALL)
    luaD_hook(L, LUA_HOOKCALL, -1);
  int n = (*f)(L);  /* do the actual call */
  api_checknelems(L, n);
  luaD_poscall(L, L->top - n);
  return 1;
}

int luaD_precallC(lua_State* L, StkId func, int nresults) {
  ptrdiff_t funcr = savestack(L, func);
  lua_CFunction f = func->getCClosure()->f;
  L->checkstack(LUA_MINSTACK);  /* ensure minimum stack size */
  CallInfo* ci = next_ci(L);  /* now 'enter' new function */
  ci->nresults = nresults;
  ci->func = restorestack(L, funcr);
  ci->top = L->top + LUA_MINSTACK;
  assert(ci->top <= L->stack_last);
  ci->callstatus = 0;
  if (L->hookmask & LUA_MASKCALL)
    luaD_hook(L, LUA_HOOKCALL, -1);
  int n = (*f)(L);  /* do the actual call */
  api_checknelems(L, n);
  luaD_poscall(L, L->top - n);
  return 1;
}

int luaD_precallLua(lua_State* L, StkId func, int nresults) {
  ptrdiff_t funcr = savestack(L, func);
  StkId base;
  Proto *p = func->getLClosure()->p;
  L->checkstack(p->maxstacksize);
  func = restorestack(L, funcr);
  int n = cast_int(L->top - func) - 1;  /* number of real arguments */
  for (; n < p->numparams; n++) {
    L->top[0] = TValue::nil;  /* complete missing arguments */
    L->top++;
  }
  base = (!p->is_vararg) ? func + 1 : adjust_varargs(L, p, n);
  CallInfo* ci = next_ci(L);  /* now 'enter' new function */
  ci->nresults = nresults;
  ci->func = func;
  ci->base = base;
  ci->top = base + p->maxstacksize;
  assert(ci->top <= L->stack_last);
  //ci->savedpc = p->code;  /* starting point */
  ci->savedpc = &p->code[0];
  ci->callstatus = CIST_LUA;
  L->top = ci->top;
  if (L->hookmask & LUA_MASKCALL)
    callhook(L, ci);
  return 0;
}

/*
** returns true if function has been executed (C function)
*/
int luaD_precall (lua_State *L, StkId func, int nresults) {
  THREAD_CHECK(L);
  switch (func->type()) {
    case LUA_TLCF: return luaD_precallLightC(L,func,nresults);
    case LUA_TCCL: return luaD_precallC(L,func,nresults);
    case LUA_TLCL: return luaD_precallLua(L,func,nresults);
    
    // Not a function.
    default:
      {
        ptrdiff_t funcr = savestack(L, func);
        func = tryfuncTM(L, func);  /* retry with 'function' tag method */
        return luaD_precall(L, func, nresults);  /* now it must be a function */
      }
  }
}


int luaD_poscall (lua_State *L, StkId firstResult) {
  THREAD_CHECK(L);
  StkId res;
  int wanted, i;
  CallInfo *ci = L->ci_;
  if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) {
    if (L->hookmask & LUA_MASKRET) {
      ptrdiff_t fr = savestack(L, firstResult);  /* hook may change stack */
      luaD_hook(L, LUA_HOOKRET, -1);
      firstResult = restorestack(L, fr);
    }
    L->oldpc = ci->previous->savedpc;  /* 'oldpc' for caller function */
  }
  res = ci->func;  /* res == final position of 1st result */
  wanted = ci->nresults;
  L->ci_ = ci = ci->previous;  /* back to caller */
  /* move results to correct place */
  for (i = wanted; i != 0 && firstResult < L->top; i--) {
    *res = *firstResult;
    res++;
    firstResult++;
  }
  while (i-- > 0) {
    *res = TValue::nil;
    res++;
  }
  L->top = res;
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
  if (++L->nCcalls >= LUAI_MAXCCALLS) {
    if (L->nCcalls == LUAI_MAXCCALLS)
      luaG_runerror("C stack overflow");
    else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3)))
      luaD_throw(LUA_ERRERR);  /* error while handing stack error */
  }
  if (!allowyield) L->nny++;
  if (!luaD_precall(L, func, nResults))  /* is a Lua function? */
    luaV_execute(L);  /* call it */
  if (!allowyield) L->nny--;
  L->nCcalls--;
  luaC_checkGC();
}


static void finishCcall (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  int n;
  assert(ci->k != NULL);  /* must have a continuation */
  assert(L->nny == 0);
  /* finish 'luaD_call' */
  L->nCcalls--;
  /* finish 'lua_callk' */
  adjustresults(L, ci->nresults);
  /* call continuation function */
  if (!(ci->callstatus & CIST_STAT))  /* no call status? */
    ci->status = LUA_YIELD;  /* 'default' status */
  assert(ci->status != LUA_OK);
  ci->callstatus = (ci->callstatus & ~(CIST_YPCALL | CIST_STAT)) | CIST_YIELDED;
  n = (*ci->k)(L);
  api_checknelems(L, n);
  /* finish 'luaD_precall' */
  luaD_poscall(L, L->top - n);
}


static void unroll (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  UNUSED(ud);
  for (;;) {
    if (L->ci_ == &L->base_ci)  /* stack is empty? */
      return;  /* coroutine finished normally */
    if (!isLua(L->ci_))  /* C function? */
      finishCcall(L);
    else {  /* Lua function */
      luaV_finishOp(L);  /* finish interrupted instruction */
      luaV_execute(L);  /* execute down to higher C 'boundary' */
    }
  }
}


/*
** check whether thread has a suspended protected call
*/
static CallInfo *findpcall (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci;
  for (ci = L->ci_; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


static int recover (lua_State *L, int status) {
  THREAD_CHECK(L);
  StkId oldtop;
  CallInfo *ci = findpcall(L);
  if (ci == NULL) return 0;  /* no recovery point */
  /* "finish" luaD_pcall */
  oldtop = restorestack(L, ci->extra);
  luaF_close(oldtop);
  seterrorobj(L, status, oldtop);
  L->ci_ = ci;
  L->allowhook = ci->old_allowhook;
  L->nny = 0;  /* should be zero to be yieldable */
  L->shrinkstack();
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
  L->top = firstArg;  /* remove args from the stack */
  L->top[0] = luaS_new(msg);  /* push error message */
  incr_top(L);
  luaD_throw(-1);  /* jump back to 'lua_resume' */
}


/*
** do the work for 'lua_resume' in protected mode
*/
static void resume (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  StkId firstArg = cast(StkId, ud);
  CallInfo *ci = L->ci_;
  if (L->nCcalls >= LUAI_MAXCCALLS)
    resume_error(L, "C stack overflow", firstArg);
  if (L->status == LUA_OK) {  /* may be starting a coroutine */
    if (ci != &L->base_ci)  /* not in base level? */
      resume_error(L, "cannot resume non-suspended coroutine", firstArg);
    /* coroutine is in base level; start running it */
    if (!luaD_precall(L, firstArg - 1, LUA_MULTRET))  /* Lua function? */
      luaV_execute(L);  /* call it */
  }
  else if (L->status != LUA_YIELD)
    resume_error(L, "cannot resume dead coroutine", firstArg);
  else {  /* resuming from previous yield */
    L->status = LUA_OK;
    if (isLua(ci))  /* yielded inside a hook? */
      luaV_execute(L);  /* just continue running Lua code */
    else {  /* 'common' yield */
      ci->func = restorestack(L, ci->extra);
      if (ci->k != NULL) {  /* does it have a continuation? */
        int n;
        ci->status = LUA_YIELD;  /* 'default' status */
        ci->callstatus |= CIST_YIELDED;
        n = (*ci->k)(L);  /* call continuation */
        api_checknelems(L, n);
        firstArg = L->top - n;  /* yield results come from continuation */
      }
      L->nCcalls--;  /* finish 'luaD_call' */
      luaD_poscall(L, firstArg);  /* finish 'luaD_precall' */
    }
    unroll(L, NULL);
  }
}


int lua_resume (lua_State *L, lua_State *from, int nargs) {
  THREAD_CHECK(L);
  int status;
  L->nCcalls = (from) ? from->nCcalls + 1 : 1;
  L->nny = 0;  /* allow yields */
  api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
  status = luaD_rawrunprotected(L, resume, L->top - nargs);
  if (status == -1)  /* error calling 'lua_resume'? */
    status = LUA_ERRRUN;
  else {  /* yield or regular error */
    while (status != LUA_OK && status != LUA_YIELD) {  /* error? */
      if (recover(L, status))  /* recover point? */
        status = luaD_rawrunprotected(L, unroll, NULL);  /* run continuation */
      else {  /* unrecoverable error */
        L->status = cast_byte(status);  /* mark thread as `dead' */
        seterrorobj(L, status, L->top);
        L->ci_->top = L->top;
        break;
      }
    }
    assert(status == L->status);
  }
  L->nny = 1;  /* do not allow yields */
  L->nCcalls--;
  assert(L->nCcalls == ((from) ? from->nCcalls : 0));
  return status;
}


int lua_yieldk (lua_State *L, int nresults, int ctx, lua_CFunction k) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  api_checknelems(L, nresults);
  if (L->nny > 0) {
    if (L != G(L)->mainthread)
      luaG_runerror("attempt to yield across metamethod/C-call boundary");
    else
      luaG_runerror("attempt to yield from outside a coroutine");
  }
  L->status = LUA_YIELD;
  if (isLua(ci)) {  /* inside a hook? */
    api_check(k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->k = k) != NULL)  /* is there a continuation? */
      ci->ctx = ctx;  /* save context */
    ci->extra = savestack(L, ci->func);  /* save current 'func' */
    ci->func = L->top - nresults - 1;  /* protect stack below results */
    luaD_throw(LUA_YIELD);
  }
  assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  return 0;  /* return to 'luaD_hook' */
}


int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  THREAD_CHECK(L);
  int status;
  CallInfo *old_ci = L->ci_;
  uint8_t old_allowhooks = L->allowhook;
  unsigned short old_nny = L->nny;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = luaD_rawrunprotected(L, func, u);
  if (status != LUA_OK) {  /* an error occurred? */
    // Error handling gets an exemption from the memory limit. Not doing so would mean that
    // reporting an out-of-memory error could itself cause another out-of-memory error, ad infinitum.
    l_memcontrol.disableLimit();

    StkId oldtop = restorestack(L, old_top);
    luaF_close(oldtop);  /* close possible pending closures */
    seterrorobj(L, status, oldtop);
    L->ci_ = old_ci;
    L->allowhook = old_allowhooks;
    L->nny = old_nny;
    L->shrinkstack();

    l_memcontrol.enableLimit();
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
struct SParser {  /* data to `f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};


static void checkmode (lua_State *L, const char *mode, const char *x) {
  THREAD_CHECK(L);
  if (mode && strchr(mode, x[0]) == NULL) {
    luaO_pushfstring(L,
       "attempt to load a %s chunk (mode is " LUA_QS ")", x, mode);
    luaD_throw(LUA_ERRSYNTAX);
  }
}


static void f_parser (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  int i;
  Proto *tf;
  Closure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  int c = zgetc(p->z);  /* read first character */
  if (c == LUA_SIGNATURE[0]) {
    checkmode(L, p->mode, "binary");
    tf = luaU_undump(L, p->z, &p->buff, p->name);
  }
  else {
    checkmode(L, p->mode, "text");
    tf = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  L->top[0] = tf;
  incr_top(L);
  cl = luaF_newLclosure(tf);
  if(cl == NULL) luaD_throw(LUA_ERRMEM);
  L->top[-1] = TValue::LClosure(cl);
  for (i = 0; i < (int)tf->upvalues.size(); i++)  /* initialize upvalues */
    cl->ppupvals_[i] = luaF_newupval();
}


int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  THREAD_CHECK(L);
  L->nny++;  /* cannot yield during parsing */

  SParser p;
  p.z = z;
  p.name = name;
  p.mode = mode;

  int status = luaD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);

  L->nny--;
  return status;
}


