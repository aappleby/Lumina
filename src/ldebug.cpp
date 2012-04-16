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
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltm.h"
#include "lvm.h"



static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name);


static int currentpc (CallInfo *ci) {
  assert(isLua(ci));
  return pcRel(ci->savedpc, ci_func(ci)->proto_);
}


static int currentline (CallInfo *ci) {
  return getfuncline(ci_func(ci)->proto_, currentpc(ci));
}


/*
** this function can be called asynchronous (e.g. during a signal)
*/
int lua_sethook (lua_State *L, lua_Hook func, int mask, int count) {
  THREAD_CHECK(L);
  if (func == NULL || mask == 0) {  /* turn off hooks? */
    mask = 0;
    func = NULL;
  }
  if (isLua(L->stack_.callinfo_))
    L->oldpc = L->stack_.callinfo_->savedpc;
  L->hook = func;
  L->basehookcount = count;
  L->hookcount = L->basehookcount;
  L->hookmask = cast_byte(mask);
  return 1;
}


lua_Hook lua_gethook (lua_State *L) {
  THREAD_CHECK(L);
  return L->hook;
}


int lua_gethookmask (lua_State *L) {
  THREAD_CHECK(L);
  return L->hookmask;
}


int lua_gethookcount (lua_State *L) {
  THREAD_CHECK(L);
  return L->basehookcount;
}


int lua_getstack (lua_State *L, int level, lua_Debug *ar) {
  THREAD_CHECK(L);
  int status;
  CallInfo *ci;
  if (level < 0) return 0;  /* invalid (negative) level */
  for (ci = L->stack_.callinfo_; level > 0 && ci != &L->stack_.callinfo_head_; ci = ci->previous)
    level--;
  if (level == 0 && ci != &L->stack_.callinfo_head_) {  /* level found? */
    status = 1;
    ar->i_ci = ci;
  }
  else status = 0;  /* no such level */
  return status;
}


static const char *upvalname (Proto *p, int uv) {
  TString *s = check_exp(uv < (int)p->upvalues.size(), p->upvalues[uv].name);
  if (s == NULL) return "?";
  else return s->c_str();
}


static const char *findvararg (CallInfo *ci, int n, StkId *pos) {
  int nparams = ci->func->getLClosure()->proto_->numparams;
  if (n >= ci->base - ci->func - nparams)
    return NULL;  /* no such vararg */
  else {
    *pos = ci->func + nparams + n;
    return "(*vararg)";  /* generic name for any vararg */
  }
}


static const char *findlocal (lua_State *L, CallInfo *ci, int n,
                              StkId *pos) {
  THREAD_CHECK(L);
  const char *name = NULL;
  StkId base;
  if (isLua(ci)) {
    if (n < 0)  /* access to vararg values? */
      return findvararg(ci, -n, pos);
    else {
      base = ci->base;
      name = luaF_getlocalname(ci_func(ci)->proto_, n, currentpc(ci));
    }
  }
  else
    base = ci->func + 1;
  if (name == NULL) {  /* no 'standard' name? */
    StkId limit = (ci == L->stack_.callinfo_) ? L->stack_.top_ : ci->next->func;
    if (limit - base >= n && n > 0)  /* is 'n' inside 'ci' stack? */
      name = "(*temporary)";  /* generic name for any valid slot */
    else
      return NULL;  /* no name */
  }
  *pos = base + (n - 1);
  return name;
}


const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n) {
  THREAD_CHECK(L);
  const char *name;
  if (ar == NULL) {  /* information about non-active function? */
    if (!L->stack_.top_[-1].isLClosure())  /* not a Lua function? */
      name = NULL;
    else  /* consider live variables at function start (parameters) */
      name = luaF_getlocalname(L->stack_.top_[-1].getLClosure()->proto_, n, 0);
  }
  else {  /* active function; get information through 'ar' */
    StkId pos = 0;  /* to avoid warnings */
    name = findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      L->stack_.top_[0] = pos[0];
      L->stack_.top_++;
    }
  }
  return name;
}


const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n) {
  THREAD_CHECK(L);
  StkId pos = 0;  /* to avoid warnings */
  const char *name = findlocal(L, ar->i_ci, n, &pos);
  if (name) {
    pos[0] = L->stack_.top_[-1];
  }
  L->stack_.pop();  /* pop value */
  return name;
}


static void funcinfo (lua_Debug *ar, Closure *cl) {
  if (cl == NULL || cl->isC) {
    ar->source = "=[C]";
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    Proto *p = cl->proto_;
    ar->source = p->source ? p->source->c_str() : "=?";
    ar->linedefined = p->linedefined;
    ar->lastlinedefined = p->lastlinedefined;
    ar->what = (ar->linedefined == 0) ? "main" : "Lua";
  }
  luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}


static void collectvalidlines (lua_State *L, Closure *f) {
  THREAD_CHECK(L);
  if (f == NULL || f->isC) {
    L->stack_.push_reserve(TValue::Nil());
  }
  else {
    int i;
    TValue v;

    Table* t = NULL;
    {
      ScopedMemChecker c;
      t = new Table();  /* new table to store active lines */
      if(t == NULL) luaD_throw(LUA_ERRMEM);
      t->linkGC(getGlobalGCHead());
      L->stack_.push_reserve(TValue(t));
    }

    v = true;
    for (i = 0; i < (int)f->proto_->lineinfo.size(); i++) {
      /* for all lines with code */
      TValue key(f->proto_->lineinfo[i]);
      t->set(key, v);  /* table[line] = true */
    }
  }
}


static int auxgetinfo (lua_State *L, const char *what, lua_Debug *ar,
                    Closure *f, CallInfo *ci) {
  THREAD_CHECK(L);
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (ci && isLua(ci)) ? currentline(ci) : -1;
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
        if (ci && !(ci->callstatus & CIST_TAIL) && isLua(ci->previous))
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

int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar) {
  THREAD_CHECK(L);
  int status;
  Closure *cl;
  CallInfo *ci;
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
    func = ci->func;
    assert(ci->func->isFunction());
  }
  cl = NULL;

  if(func->isCClosure()) cl = func->getCClosure();
  if(func->isLClosure()) cl = func->getLClosure();

  status = auxgetinfo(L, what, ar, cl, ci);
  if (strchr(what, 'f')) {
    L->stack_.push_reserve(*func);
  }
  if (strchr(what, 'L'))
    collectvalidlines(L, cl);
  return status;
}


/*
** {======================================================
** Symbolic Execution
** =======================================================
*/

static const char *getobjname (Proto *p, int lastpc, int reg,
                               const char **name);


/*
** find a "name" for the RK value 'c'
*/
static void kname (Proto *p, int pc, int c, const char **name) {
  if (ISK(c)) {  /* is 'c' a constant? */
    TValue *kvalue = &p->constants[INDEXK(c)];
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
static int findsetreg (Proto *p, int lastpc, int reg) {
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


static const char *getobjname (Proto *p, int lastpc, int reg,
                               const char **name) {
  int pc;
  *name = luaF_getlocalname(p, reg + 1, lastpc);
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
                         ? luaF_getlocalname(p, t + 1, pc)
                         : upvalname(p, t);
        kname(p, pc, k, name);
        return (vn && strcmp(vn, LUA_ENV) == 0) ? "global" : "field";
      }
      case OP_GETUPVAL: {
        *name = upvalname(p, GETARG_B(i));
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


static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name) {
  THREAD_CHECK(L);
  TMS tm;
  Proto *p = ci_func(ci)->proto_;  /* calling function */
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
static int isinstack (CallInfo *ci, const TValue *o) {
  StkId p;
  for (p = ci->base; p < ci->top; p++)
    if (o == p) return 1;
  return 0;
}


static const char *getupvalname (CallInfo *ci, const TValue *o,
                                 const char **name) {
  Closure *c = ci_func(ci);
  int i;
  for (i = 0; i < c->nupvalues; i++) {
    if (c->ppupvals_[i]->v == o) {
      *name = upvalname(c->proto_, i);
      return "upvalue";
    }
  }
  return NULL;
}


l_noret luaG_typeerror (const TValue *o, const char *op) {
  lua_State*L = thread_L;
  CallInfo *ci = L->stack_.callinfo_;
  const char *name = NULL;
  const char *t = objtypename(o);
  const char *kind = NULL;
  if (isLua(ci)) {
    kind = getupvalname(ci, o, &name);  /* check whether 'o' is an upvalue */
    if (!kind && isinstack(ci, o)) {
      /* no? try a register */
      kind = getobjname(ci_func(ci)->proto_, currentpc(ci), cast_int(o - ci->base), &name);
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


l_noret luaG_ordererror (const TValue *p1, const TValue *p2) {
  const char *t1 = objtypename(p1);
  const char *t2 = objtypename(p2);
  if (t1 == t2)
    luaG_runerror("attempt to compare two %s values", t1);
  else
    luaG_runerror("attempt to compare %s with %s", t1, t2);
}


static void addinfo (const char *msg) {
  lua_State* L = thread_L;
  CallInfo *ci = L->stack_.callinfo_;
  if (isLua(ci)) {  /* is Lua code? */
    char buff[LUA_IDSIZE];  /* add file:line information */
    int line = currentline(ci);
    TString *src = ci_func(ci)->proto_->source;
    if (src)
      luaO_chunkid(buff, src->c_str(), LUA_IDSIZE);
    else {  /* no source available; use "?" instead */
      buff[0] = '?'; buff[1] = '\0';
    }
    luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
  }
}


l_noret luaG_errormsg () {
  lua_State *L = thread_L;
  if (L->errfunc != 0) {  /* is there an error handling function? */
    StkId errfunc = restorestack(L, L->errfunc);
    if (!errfunc->isFunction()) luaD_throw(LUA_ERRERR);
    L->stack_.top_[0] = L->stack_.top_[-1];  /* move argument */
    L->stack_.top_[-1] = *errfunc;  /* push function */
    incr_top(L);
    luaD_call(L, L->stack_.top_ - 2, 1, 0);  /* call it */
  }
  luaD_throw(LUA_ERRRUN);
}


l_noret luaG_runerror (const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  addinfo(luaO_pushvfstring(fmt, argp));
  va_end(argp);
  luaG_errormsg();
}

