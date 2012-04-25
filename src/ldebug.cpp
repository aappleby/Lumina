/*
** $Id: ldebug.c,v 2.88 2011/11/30 12:43:51 roberto Exp $
** Debug Interface
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>


#define ldebug_c

#include "lua.h"

#include "lapi.h"
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "ltm.h"
#include "lvm.h"



static const char *getfuncname (LuaThread *L, LuaStackFrame *ci, const char **name);


static int currentpc (LuaStackFrame *ci) {
  assert(ci->isLua());
  return pcRel(ci->savedpc, ci->getFunc()->getLClosure()->proto_);
}


static int currentline (LuaStackFrame *ci) {
  return getfuncline(ci->getFunc()->getLClosure()->proto_, currentpc(ci));
}


/*
** this function can be called asynchronous (e.g. during a signal)
*/
int lua_sethook (LuaThread *L, LuaHook func, int mask, int count) {
  THREAD_CHECK(L);
  if (func == NULL || mask == 0) {  /* turn off hooks? */
    mask = 0;
    func = NULL;
  }
  if (L->stack_.callinfo_->isLua())
    L->oldpc = L->stack_.callinfo_->savedpc;
  L->hook = func;
  L->basehookcount = count;
  L->hookcount = L->basehookcount;
  L->hookmask = cast_byte(mask);
  return 1;
}


LuaHook lua_gethook (LuaThread *L) {
  THREAD_CHECK(L);
  return L->hook;
}


int lua_gethookmask (LuaThread *L) {
  THREAD_CHECK(L);
  return L->hookmask;
}


int lua_gethookcount (LuaThread *L) {
  THREAD_CHECK(L);
  return L->basehookcount;
}


int lua_getstack (LuaThread *L, int level, LuaDebug *ar) {
  THREAD_CHECK(L);
  int status;
  LuaStackFrame *ci;
  if (level < 0) return 0;  /* invalid (negative) level */
  for (ci = L->stack_.callinfo_; level > 0 && ci != L->stack_.callinfo_head_; ci = ci->previous)
    level--;
  if (level == 0 && ci != L->stack_.callinfo_head_) {  /* level found? */
    status = 1;
    ar->i_ci = ci;
  }
  else status = 0;  /* no such level */
  return status;
}


static const char *findvararg (LuaStackFrame *ci, int n, StkId *pos) {
  int nparams = ci->getFunc()->getLClosure()->proto_->numparams;
  if (n >= ci->getBase() - ci->getFunc() - nparams)
    return NULL;  /* no such vararg */
  else {
    *pos = ci->getFunc() + nparams + n;
    return "(*vararg)";  /* generic name for any vararg */
  }
}


static const char *findlocal (LuaThread *L, LuaStackFrame *ci, int n,
                              StkId *pos) {
  THREAD_CHECK(L);
  const char *name = NULL;
  StkId base;
  if (ci->isLua()) {
    if (n < 0)  /* access to vararg values? */
      return findvararg(ci, -n, pos);
    else {
      base = ci->getBase();
      name = ci->getFunc()->getLClosure()->proto_->getLocalName(n, currentpc(ci));
    }
  }
  else
    base = ci->getFunc() + 1;
  if (name == NULL) {  /* no 'standard' name? */
    StkId limit = (ci == L->stack_.callinfo_) ? L->stack_.top_ : ci->next->getFunc();
    if (limit - base >= n && n > 0)  /* is 'n' inside 'ci' stack? */
      name = "(*temporary)";  /* generic name for any valid slot */
    else
      return NULL;  /* no name */
  }
  *pos = base + (n - 1);
  return name;
}


const char *lua_getlocal (LuaThread *L, const LuaDebug *ar, int n) {
  THREAD_CHECK(L);
  const char *name;
  if (ar == NULL) {  /* information about non-active function? */
    if (!L->stack_.top_[-1].isLClosure())  /* not a Lua function? */
      name = NULL;
    else  /* consider live variables at function start (parameters) */
      name = L->stack_.top_[-1].getLClosure()->proto_->getLocalName(n, 0);
  }
  else {  /* active function; get information through 'ar' */
    StkId pos = 0;  /* to avoid warnings */
    name = findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      L->stack_.push(pos[0]);
    }
  }
  return name;
}


const char *lua_setlocal (LuaThread *L, const LuaDebug *ar, int n) {
  THREAD_CHECK(L);
  StkId pos = 0;  /* to avoid warnings */
  const char *name = findlocal(L, ar->i_ci, n, &pos);
  if (name) {
    pos[0] = L->stack_.top_[-1];
  }
  L->stack_.pop();  /* pop value */
  return name;
}


static void funcinfo (LuaDebug *ar, LuaClosure *cl) {
  if (cl == NULL || cl->isC) {
    ar->source = "=[C]";
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    LuaProto *p = cl->proto_;
    ar->source = p->source ? p->source->c_str() : "=?";
    ar->linedefined = p->linedefined;
    ar->lastlinedefined = p->lastlinedefined;
    ar->what = (ar->linedefined == 0) ? "main" : "Lua";
  }
  luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}


static void collectvalidlines (LuaThread *L, LuaClosure *f) {
  THREAD_CHECK(L);
  if (f == NULL || f->isC) {
    LuaResult result = L->stack_.push_reserve2(LuaValue::Nil());
    handleResult(result);
  }
  else {
    LuaTable* t = new LuaTable();  /* new table to store active lines */
    LuaResult result = L->stack_.push_reserve2(LuaValue(t));
    handleResult(result);

    LuaValue v = LuaValue(true);
    for (int i = 0; i < (int)f->proto_->lineinfo.size(); i++) {
      /* for all lines with code */
      LuaValue key(f->proto_->lineinfo[i]);
      t->set(key, v);  /* table[line] = true */
    }
  }
}


static int auxgetinfo (LuaThread *L, const char *what, LuaDebug *ar,
                    LuaClosure *f, LuaStackFrame *ci) {
  THREAD_CHECK(L);
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (ci && ci->isLua()) ? currentline(ci) : -1;
        break;
      }
      case 'u': {
        ar->nups = (f == NULL) ? 0 : f->nupvalues;
        if (f == NULL || f->isC) {
          ar->isvararg = 1;
          ar->nparams = 0;
        }
        else {
          ar->isvararg = f->proto_->is_vararg;
          ar->nparams = f->proto_->numparams;
        }
        break;
      }
      case 't': {
        ar->istailcall = (ci) ? ci->callstatus & CIST_TAIL : 0;
        break;
      }
      case 'n': {
        /* calling function is a known Lua function? */
        if (ci && !(ci->callstatus & CIST_TAIL) && ci->previous->isLua())
          ar->namewhat = getfuncname(L, ci->previous, &ar->name);
        else
          ar->namewhat = NULL;
        if (ar->namewhat == NULL) {
          ar->namewhat = "";  /* not found */
          ar->name = NULL;
        }
        break;
      }
      case 'L':
      case 'f':  /* handled by lua_getinfo */
        break;
      default: status = 0;  /* invalid option */
    }
  }
  return status;
}

// what does this do?

int lua_getinfo (LuaThread *L, const char *what, LuaDebug *ar) {
  THREAD_CHECK(L);
  int status;
  LuaClosure *cl;
  LuaStackFrame *ci;
  StkId func;
  if (*what == '>') {
    ci = NULL;
    func = L->stack_.top_ - 1;
    api_check(func->isFunction(), "function expected");
    what++;  /* skip the '>' */
    L->stack_.pop();  /* pop function */
  }
  else {
    ci = ar->i_ci;
    func = ci->getFunc();
    assert(ci->getFunc()->isFunction());
  }
  cl = NULL;

  if(func->isCClosure()) cl = func->getCClosure();
  if(func->isLClosure()) cl = func->getLClosure();

  status = auxgetinfo(L, what, ar, cl, ci);
  
  LuaResult result = LUA_OK;
  if (strchr(what, 'f')) {
    result = L->stack_.push_reserve2(*func);
  }
  handleResult(result);

  if (strchr(what, 'L')) {
    collectvalidlines(L, cl);
  }

  return status;
}


/*
** {======================================================
** Symbolic Execution
** =======================================================
*/

static const char *getobjname (LuaProto *p, int lastpc, int reg,
                               const char **name);


/*
** find a "name" for the RK value 'c'
*/
static void kname (LuaProto *p, int pc, int c, const char **name) {
  if (ISK(c)) {  /* is 'c' a constant? */
    LuaValue *kvalue = &p->constants[INDEXK(c)];
    if (kvalue->isString()) {  /* literal constant? */
      *name = kvalue->getString()->c_str();  /* it is its own name */
      return;
    }
    /* else no reasonable name found */
  }
  else {  /* 'c' is a register */
    const char *what = getobjname(p, pc, c, name); /* search for 'c' */
    if (what && *what == 'c') {  /* found a constant name? */
      return;  /* 'name' already filled */
    }
    /* else no reasonable name found */
  }
  *name = "?";  /* no reasonable name found */
}


/*
** try to find last instruction before 'lastpc' that modified register 'reg'
*/
static int findsetreg (LuaProto *p, int lastpc, int reg) {
  int pc;
  int setreg = -1;  /* keep last instruction that changed 'reg' */
  for (pc = 0; pc < lastpc; pc++) {
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);
    switch (op) {
      case OP_LOADNIL: {
        int b = GETARG_B(i);
        if (a <= reg && reg <= a + b)  /* set registers from 'a' to 'a+b' */
          setreg = pc;
        break;
      }
      case OP_TFORCALL: {
        if (reg >= a + 2) setreg = pc;  /* affect all regs above its base */
        break;
      }
      case OP_CALL:
      case OP_TAILCALL: {
        if (reg >= a) setreg = pc;  /* affect all registers above base */
        break;
      }
      case OP_JMP: {
        int b = GETARG_sBx(i);
        int dest = pc + 1 + b;
        /* jump is forward and do not skip `lastpc'? */
        if (pc < dest && dest <= lastpc)
          pc += b;  /* do the jump */
        break;
      }
      case OP_TEST: {
        if (reg == a) setreg = pc;  /* jumped code can change 'a' */
        break;
      }
      default:
        if (testAMode(op) && reg == a)  /* any instruction that set A */
          setreg = pc;
        break;
    }
  }
  return setreg;
}


static const char *getobjname (LuaProto *p, int lastpc, int reg,
                               const char **name) {
  int pc;
  *name = p->getLocalName(reg + 1, lastpc);
  if (*name)  /* is a local? */
    return "local";
  /* else try symbolic execution */
  pc = findsetreg(p, lastpc, reg);
  if (pc != -1) {  /* could find instruction? */
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_MOVE: {
        int b = GETARG_B(i);  /* move from 'b' to 'a' */
        if (b < GETARG_A(i))
          return getobjname(p, pc, b, name);  /* get name for 'b' */
        break;
      }
      case OP_GETTABUP:
      case OP_GETTABLE: {
        int k = GETARG_C(i);  /* key index */
        int t = GETARG_B(i);  /* table index */
        const char *vn = (op == OP_GETTABLE)  /* name of indexed variable */
                         ? p->getLocalName(t + 1, pc)
                         : p->getUpvalName(t);
        kname(p, pc, k, name);
        return (vn && strcmp(vn, LUA_ENV) == 0) ? "global" : "field";
      }
      case OP_GETUPVAL: {
        *name = p->getUpvalName(GETARG_B(i));
        return "upvalue";
      }
      case OP_LOADK:
      case OP_LOADKX: {
        int b = (op == OP_LOADK) ? GETARG_Bx(i)
                                 : GETARG_Ax(p->code[pc + 1]);
        if (p->constants[b].isString()) {
          *name = p->constants[b].getString()->c_str();
          return "constant";
        }
        break;
      }
      case OP_SELF: {
        int k = GETARG_C(i);  /* key index */
        kname(p, pc, k, name);
        return "method";
      }
      default: break;  /* go through to return NULL */
    }
  }
  return NULL;  /* could not find reasonable name */
}


static const char *getfuncname (LuaThread *L, LuaStackFrame *ci, const char **name) {
  THREAD_CHECK(L);
  TMS tm;
  LuaProto *p = ci->getFunc()->getLClosure()->proto_;  /* calling function */
  int pc = currentpc(ci);  /* calling instruction index */
  Instruction i = p->code[pc];  /* calling instruction */
  switch (GET_OPCODE(i)) {
    case OP_CALL:
    case OP_TAILCALL:  /* get function name */
      return getobjname(p, pc, GETARG_A(i), name);
    case OP_TFORCALL: {  /* for iterator */
      *name = "for iterator";
       return "for iterator";
    }
    /* all other instructions can call only through metamethods */
    case OP_SELF:
    case OP_GETTABUP:
    case OP_GETTABLE: tm = TM_INDEX; break;
    case OP_SETTABUP:
    case OP_SETTABLE: tm = TM_NEWINDEX; break;
    case OP_EQ: tm = TM_EQ; break;
    case OP_ADD: tm = TM_ADD; break;
    case OP_SUB: tm = TM_SUB; break;
    case OP_MUL: tm = TM_MUL; break;
    case OP_DIV: tm = TM_DIV; break;
    case OP_MOD: tm = TM_MOD; break;
    case OP_POW: tm = TM_POW; break;
    case OP_UNM: tm = TM_UNM; break;
    case OP_LEN: tm = TM_LEN; break;
    case OP_LT: tm = TM_LT; break;
    case OP_LE: tm = TM_LE; break;
    case OP_CONCAT: tm = TM_CONCAT; break;
    default:
      return NULL;  /* else no useful name can be found */
  }
  *name = G(L)->tagmethod_names_[tm]->c_str();
  return "metamethod";
}

/* }====================================================== */



/*
** only ANSI way to check whether a pointer points to an array
** (used only for error messages, so efficiency is not a big concern)
*/
static int isinstack (LuaStackFrame *ci, const LuaValue *o) {
  StkId p;
  for (p = ci->getBase(); p < ci->getTop(); p++) {
    if (o == p) return 1;
  }
  return 0;
}


static const char *getupvalname (LuaStackFrame *ci, const LuaValue *o, const char **name) {
  LuaClosure *c = ci->getFunc()->getLClosure();
  int i;
  for (i = 0; i < c->nupvalues; i++) {
    if (c->ppupvals_[i]->v == o) {
      *name = c->proto_->getUpvalName(i);
      return "upvalue";
    }
  }
  return NULL;
}


l_noret luaG_typeerror (const LuaValue *o, const char *op) {
  LuaThread*L = thread_L;
  LuaStackFrame *ci = L->stack_.callinfo_;
  const char *name = NULL;
  const char *t = objtypename(o);
  const char *kind = NULL;
  if (ci->isLua()) {
    kind = getupvalname(ci, o, &name);  /* check whether 'o' is an upvalue */
    if (!kind && isinstack(ci, o)) {
      /* no? try a register */
      kind = getobjname(ci->getFunc()->getLClosure()->proto_, currentpc(ci), cast_int(o - ci->getBase()), &name);
    }
  }
  if (kind)
    luaG_runerror("attempt to %s %s " LUA_QS " (a %s value)",
                op, kind, name, t);
  else
    luaG_runerror("attempt to %s a %s value", op, t);
}


l_noret luaG_concaterror (StkId p1, StkId p2) {
  if (p1->isString() || p1->isNumber()) p1 = p2;
  assert(!p1->isString() && !p2->isNumber());
  luaG_typeerror(p1, "concatenate");
}


l_noret luaG_ordererror (const LuaValue *p1, const LuaValue *p2) {
  const char *t1 = objtypename(p1);
  const char *t2 = objtypename(p2);
  if (t1 == t2)
    luaG_runerror("attempt to compare two %s values", t1);
  else
    luaG_runerror("attempt to compare %s with %s", t1, t2);
}


static void addinfo (const char *msg) {
  LuaThread* L = thread_L;
  LuaStackFrame *ci = L->stack_.callinfo_;
  if (ci->isLua()) {  /* is Lua code? */
    char buff[LUA_IDSIZE];  /* add file:line information */
    int line = currentline(ci);
    LuaString *src = ci->getFunc()->getLClosure()->proto_->source;
    if (src)
      luaO_chunkid(buff, src->c_str(), LUA_IDSIZE);
    else {  /* no source available; use "?" instead */
      buff[0] = '?'; buff[1] = '\0';
    }
    luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
  }
}


l_noret luaG_errormsg () {
  LuaThread *L = thread_L;
  if (L->errfunc != 0) {  /* is there an error handling function? */
    StkId errfunc = restorestack(L, L->errfunc);
    if (!errfunc->isFunction()) throwError(LUA_ERRERR);
    L->stack_.top_[0] = L->stack_.top_[-1];  /* move argument */
    L->stack_.top_[-1] = *errfunc;  /* push function */
    L->stack_.top_++;
    LuaResult result = L->stack_.reserve2(0);
    handleResult(result);
    luaD_call(L, L->stack_.top_ - 2, 1, 0);  /* call it */
  }
  throwError(LUA_ERRRUN);
}


l_noret luaG_runerror (const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  addinfo(luaO_pushvfstring(fmt, argp));
  va_end(argp);
  luaG_errormsg();
}

