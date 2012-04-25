/*
** $Id: lapi.c,v 2.159 2011/11/30 12:32:05 roberto Exp $
** Lua API
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <stdarg.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


LuaValue index2addr3(LuaThread* L, int idx) {
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;
  if (idx > 0) {
    LuaValue *o = ci->getFunc() + idx;
    if (o >= L->stack_.top_) {
      return LuaValue::None();
    }
    else return *o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return L->stack_.top_[idx];
  }

  if (idx == LUA_REGISTRYINDEX) {
    return thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (ci->getFunc()->isLightFunction()) {
    return LuaValue::None();
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  LuaClosure* func = ci->getFunc()->getCClosure();
  if(idx < func->nupvalues) {
    return func->pupvals_[idx];
  }

  // Invalid stack index.
  return LuaValue::None();
}

LuaValue* index2addr2 (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;
  if (idx > 0) {
    LuaValue *o = ci->getFunc() + idx;
    if (o >= L->stack_.top_) {
      return NULL;
    }
    else return o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return L->stack_.top_ + idx;
  }

  if (idx == LUA_REGISTRYINDEX) {
    return &thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (ci->getFunc()->isLightFunction()) {
    return NULL;
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  LuaClosure* func = ci->getFunc()->getCClosure();
  if(idx < func->nupvalues) {
    return &func->pupvals_[idx];
  }

  // Invalid stack index.
  return NULL;
}

LuaValue *index2addr (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  LuaValue* result = index2addr2(L,idx);
  return result;
}

LuaValue* index2addr_checked(LuaThread* L, int idx) {
  THREAD_CHECK(L);
  LuaValue* result = index2addr2(L,idx);
  assert(result && "invalid index");
  return result;
}

// Expand the current stack frame so it can hold 'size' elements,
// extending the stack if needed.
int lua_checkstack (LuaThread *L, int size) {
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;

  /* stack large enough? */
  LuaValue* newtop = L->stack_.top_ + size;
  if (newtop < L->stack_.last()) {
    if (ci->getTop() < newtop) {
      ci->setTop(newtop);
    }
    return 1;
  }

  /* no; need to grow stack */
  int inuse = cast_int(L->stack_.top_ - L->stack_.begin()) + EXTRA_STACK;
  
  /* can grow without overflow? */
  if (inuse + size > LUAI_MAXSTACK) {
    /* no */
    return 0;
  }

  /* try to grow stack */
  try {
    LuaResult result = L->stack_.grow2(size);
    handleResult(result);
  }
  catch(...) { 
    return 0;
  }

  // adjust frame top
  newtop = L->stack_.top_ + size;
  if (ci->getTop() < newtop) {
    ci->setTop(newtop);
  }
  return 1;
}


void lua_xmove (LuaThread *from, LuaThread *to, int n) {
  THREAD_CHECK(from);
  int i;
  if (from == to) return;
  from->stack_.checkArgs(n);

  api_check(G(from) == G(to), "moving among independent states");
  api_check(to->stack_.callinfo_->getTop() - to->stack_.top_ >= n, "not enough elements to move");
  from->stack_.top_ -= n;

  for (i = 0; i < n; i++) {
    to->stack_.push(from->stack_.top_[i]);
  }
}


LuaCallback lua_atpanic (LuaThread *L, LuaCallback panicf) {
  THREAD_CHECK(L);
  LuaCallback old;
  old = G(L)->panic;
  G(L)->panic = panicf;
  return old;
}


const double *lua_version (LuaThread *L) {
  //THREAD_CHECK(L);
  static const double version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}


/*
** convert an acceptable stack index into an absolute index
*/
int lua_absindex (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  return (idx > 0 || idx <= LUA_REGISTRYINDEX)
         ? idx
         : cast_int(L->stack_.top_ - L->stack_.callinfo_->getFunc() + idx);
}

void lua_insert (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId p;
  StkId q;
  p = index2addr_checked(L, idx);
  for (q = L->stack_.top_; q>p; q--) {
    q[0] = q[-1];
  }
  p[0] = L->stack_.top_[0];
}


static void moveto (LuaThread *L, LuaValue *fr, int idx) {
  THREAD_CHECK(L);
  LuaValue *to = index2addr_checked(L, idx);
  *to = *fr;
  if (idx < LUA_REGISTRYINDEX) {  /* function upvalue? */
    luaC_barrier(L->stack_.callinfo_->getFunc()->getCClosure(), *fr);
  }
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
}


void lua_replace (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  L->stack_.checkArgs(1);
  moveto(L, L->stack_.top_ - 1, idx);
  L->stack_.pop();
}


void lua_copy (LuaThread *L, int fromidx, int toidx) {
  THREAD_CHECK(L);
  LuaValue *fr;
  fr = index2addr_checked(L, fromidx);
  moveto(L, fr, toidx);
}



/*
** access functions (stack -> C)
*/


int lua_type (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  LuaValue v = index2addr3(L, idx);
  return v.type();
}


const char *lua_typename (LuaThread *L, int t) {
  THREAD_CHECK(L);
  UNUSED(L);
  return ttypename(t);
}


int lua_iscfunction (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr2(L, idx);
  return o && (o->isLightFunction() || o->isCClosure());
}

// To determine if a string can be converted to a number, we convert it to a number. :)
int lua_isNumberable (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  const LuaValue *v1 = index2addr2(L, idx);
  if(v1 == NULL) return 0;
  LuaValue v2 = v1->convertToNumber();
  return v2.isNone() ? 0 : 1;
}


int lua_isStringable (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  LuaValue* v = index2addr2(L,idx);
  return v && (v->isString() || v->isNumber());
}


int lua_isuserdata (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  const LuaValue *o = index2addr(L, idx);
  return o && (o->isUserdata() || o->isLightUserdata());
}


int lua_rawequal (LuaThread *L, int index1, int index2) {
  THREAD_CHECK(L);
  LuaValue v1 = index2addr3(L, index1);
  LuaValue v2 = index2addr3(L, index2);
  if(v1.isNone()) return 0;
  if(v2.isNone()) return 0;
  return (v1 == v2);
}


void  lua_arith (LuaThread *L, int op) {
  THREAD_CHECK(L);
  StkId o1;  /* 1st operand */
  StkId o2;  /* 2nd operand */
  if (op != LUA_OPUNM) /* all other operations expect two operands */
    L->stack_.checkArgs(2);
  else {  /* for unary minus, add fake 2nd operand */
    L->stack_.checkArgs(1);
    L->stack_.push(L->stack_.top_[-1]);
  }
  o1 = L->stack_.top_ - 2;
  o2 = L->stack_.top_ - 1;
  if (o1->isNumber() && o2->isNumber()) {
    o1[0] = luaO_arith(op, o1->getNumber(), o2->getNumber());
  }
  else
    luaV_arith(L, o1, o1, o2, cast(TMS, op - LUA_OPADD + TM_ADD));
  L->stack_.pop();
}

int lua_compare (LuaThread *L, int index1, int index2, int op) {
  THREAD_CHECK(L);
  LuaValue v1 = index2addr3(L, index1);
  LuaValue v2 = index2addr3(L, index2);
  if(v1.isNone()) return 0;
  if(v2.isNone()) return 0;

  switch (op) {
    case LUA_OPEQ: return luaV_equalobj_(L, &v1, &v2); break;
    case LUA_OPLT: return luaV_lessthan(L, &v1, &v2); break;
    case LUA_OPLE: return luaV_lessequal(L, &v1, &v2); break;
    default: api_check(0, "invalid option"); return 0;
  }
}

double lua_tonumberx (LuaThread *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  LuaValue v1 = index2addr3(L, idx);
  LuaValue v2 = v1.convertToNumber();

  if(v2.isNumber()) {
    if (isnum) *isnum = 1;
    return v2.getNumber();
  } else {
    if(isnum) *isnum = 0;
    return 0;
  }
}


ptrdiff_t lua_tointegerx (LuaThread *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  LuaValue v1 = index2addr3(L, idx);
  LuaValue v2 = v1.convertToNumber();

  if(v2.isNumber()) {
    if (isnum) *isnum = 1;
    return (ptrdiff_t)v2.getNumber();
  } else {
    if(isnum) *isnum = 0;
    return 0;
  }
}


uint32_t lua_tounsignedx (LuaThread *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  LuaValue v1 = index2addr3(L, idx);
  LuaValue v2 = v1.convertToNumber();

  if(v2.isNumber()) {
    if (isnum) *isnum = 1;
    return (uint32_t)v2.getNumber();
  } else {
    if(isnum) *isnum = 0;
    return 0;
  }
}


int lua_toboolean (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  const LuaValue *o = index2addr(L, idx);
  return o && o->isTrue();
}


const char *lua_tolstring (LuaThread *L, int idx, size_t *len) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);

  if(o == NULL) return NULL;

  if(o->isString()) {
    if (len != NULL) *len = o->getString()->getLen();
    return o->getString()->c_str();
  }

  if (!o->isNumber()) {
    if (len != NULL) *len = 0;
    return NULL;
  }

  double n = o->getNumber();

  char s[LUAI_MAXNUMBER2STR];
  int l = lua_number2str(s, n);
  *o = thread_G->strings_->Create(s,l);

  o = index2addr(L, idx);  /* luaC_checkGC may reallocate the stack */
  if(o == NULL) return NULL;
  if (len != NULL) *len = o->getString()->getLen();
  return o->getString()->c_str();
}

size_t lua_rawlen (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if(o == NULL) return NULL;
  switch (o->type()) {
    case LUA_TSTRING: return o->getString()->getLen();
    case LUA_TBLOB: return o->getUserdata()->len_;
    case LUA_TTABLE: return o->getTable()->getLength();
    default: return 0;
  }
}


LuaCallback lua_tocfunction (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if(o == NULL) return NULL;
  if (o->isLightFunction()) return o->getLightFunction();
  else if (o->isCClosure())
    return o->getCClosure()->cfunction_;
  else return NULL;  /* not a C function */
}


void *lua_touserdata (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if(o == NULL) return NULL;
  switch (o->type()) {
    case LUA_TBLOB: return (o->getUserdata()->buf_);
    case LUA_TVOID: return o->getLightUserdata();
    default: return NULL;
  }
}


LuaThread *lua_tothread (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if(o == NULL) return NULL;
  return (!o->isThread()) ? NULL : o->getThread();
}


const void *lua_topointer (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if(o == NULL) return NULL;
  switch (o->type()) {
    case LUA_TTABLE: return o->getTable();
    case LUA_TLCL: return o->getLClosure();
    case LUA_TCCL: return o->getCClosure();
    case LUA_TCALLBACK: return cast(void *, cast(size_t, o->getLightFunction()));
    case LUA_TTHREAD: return o->getThread();
    case LUA_TBLOB:
    case LUA_TVOID:
      return lua_touserdata(L, idx);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


void lua_pushnumber (LuaThread *L, double n) {
  THREAD_CHECK(L);
  L->stack_.push(LuaValue(n));
}


void lua_pushinteger (LuaThread *L, ptrdiff_t n) {
  THREAD_CHECK(L);
  LuaResult result = L->stack_.push_reserve2(LuaValue(n));
  handleResult(result);
}


void lua_pushunsigned (LuaThread *L, uint32_t u) {
  THREAD_CHECK(L);
  LuaResult result = L->stack_.push_reserve2(LuaValue(u));
  handleResult(result);
}


const char *lua_pushlstring (LuaThread *L, const char *s, size_t len) {
  THREAD_CHECK(L);
  LuaString* ts = thread_G->strings_->Create(s, len);
  L->stack_.push(LuaValue(ts));
  return ts->c_str();
}


const char *lua_pushstring (LuaThread *L, const char *s) {
  THREAD_CHECK(L);
  if (s == NULL) {
    L->stack_.push(LuaValue::Nil());
    return NULL;
  }

  LuaString* ts = thread_G->strings_->Create(s);
  L->stack_.push(LuaValue(ts));
  return ts->c_str();
}


const char *lua_pushvfstring (LuaThread *L, const char *fmt,
                                      va_list argp) {
  THREAD_CHECK(L);
  const char *ret;
  ret = luaO_pushvfstring(fmt, argp);
  return ret;
}


const char *lua_pushfstring (LuaThread *L, const char *fmt, ...) {
  THREAD_CHECK(L);

  va_list argp;
  va_start(argp, fmt);
  const char* ret = luaO_pushvfstring(fmt, argp);
  va_end(argp);

  return ret;
}


void lua_pushcclosure (LuaThread *L, LuaCallback fn, int n) {
  THREAD_CHECK(L);
  if (n == 0) {
    L->stack_.push(LuaValue::LightFunction(fn));
    return;
  }

  L->stack_.checkArgs(n);
  api_check(n <= MAXUPVAL, "upvalue index too large");

  LuaClosure *cl = new LuaClosure(fn, n);
  L->stack_.top_ -= n;
  while (n--) {
    cl->pupvals_[n] = L->stack_.top_[n];
  }
  L->stack_.push(LuaValue(cl));
}


void lua_pushboolean (LuaThread *L, int b) {
  THREAD_CHECK(L);
  L->stack_.push(LuaValue(b ? true : false));
}


void lua_pushlightuserdata (LuaThread *L, void *p) {
  THREAD_CHECK(L);
  L->stack_.push(LuaValue::LightUserdata((void*)p));
}


int lua_pushthread (LuaThread *L) {
  THREAD_CHECK(L);
  L->stack_.push(LuaValue(L));
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/


void lua_getglobal (LuaThread *L, const char *var) {
  THREAD_CHECK(L);

  LuaValue globals = thread_G->getRegistry()->get(LuaValue(LUA_RIDX_GLOBALS));
  L->stack_.push(LuaValue(thread_G->strings_->Create(var)));
  
  LuaValue val;
  LuaResult r = luaV_gettable2(L, globals, L->stack_.top_[-1], val);

  if(r == LUA_OK) {
    L->stack_.top_[-1] = val;
  } else {
    handleResult(r, &globals);
  }
}


void lua_gettable (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr_checked(L, idx);

  LuaValue val;
  LuaResult r = luaV_gettable2(L, *t, L->stack_.top_[-1], val);

  if(r == LUA_OK) {
    L->stack_.top_[-1] = val;
  } else {
    handleResult(r, t);
  }
}


void lua_getfield (LuaThread *L, int idx, const char *k) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr_checked(L, idx);

  LuaString* s = thread_G->strings_->Create(k);
  L->stack_.push(LuaValue(s));

  LuaValue val;
  LuaResult r = luaV_gettable2(L, *t, L->stack_.top_[-1], val);

  if(r == LUA_OK) {
    L->stack_.top_[-1] = val;
  } else {
    handleResult(r, t);
  }
}


void lua_rawget (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");

  LuaValue result = t->getTable()->get(L->stack_.top_[-1]);
  L->stack_.top_[-1] = result.isNone() ? LuaValue::Nil() : result;
}


void lua_rawgeti (LuaThread *L, int idx, int n) {
  THREAD_CHECK(L);
  StkId t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");

  LuaValue result = t->getTable()->get(LuaValue(n));
  if(result.isNone()) result = LuaValue::Nil();
  L->stack_.push(result);
}


void lua_rawgetp (LuaThread *L, int idx, const void *p) {
  THREAD_CHECK(L);
  StkId t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");
  
  LuaValue result = t->getTable()->get( LuaValue::LightUserdata(p) );
  if(result.isNone()) result = LuaValue::Nil();
  L->stack_.push(result);
}


void lua_createtable (LuaThread *L, int narray, int nrec) {
  THREAD_CHECK(L);

  LuaTable* t = new LuaTable(narray, nrec);
  L->stack_.push(LuaValue(t));
}

LuaTable* lua_getmetatable(LuaValue v) {
  int type = v.type();
  switch (type) {
    case LUA_TTABLE:    return v.getTable()->metatable;
    case LUA_TBLOB: return v.getUserdata()->metatable_;
    default:            return thread_G->base_metatables_[type];
  }
}

int lua_getmetatable (LuaThread *L, int objindex) {
  THREAD_CHECK(L);
  const LuaValue *obj;
  int res;
  obj = index2addr(L, objindex);
  if(obj == NULL) return 0;
  LuaTable* mt = lua_getmetatable(*obj);
  if (mt == NULL)
    res = 0;
  else {
    L->stack_.push(LuaValue(mt));
    res = 1;
  }
  return res;
}


void lua_getuservalue (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o;
  o = index2addr_checked(L, idx);
  api_check(o->isUserdata(), "userdata expected");
  if (o->getUserdata()->env_) {
    L->stack_.push(LuaValue(o->getUserdata()->env_));
  } else {
    L->stack_.push(LuaValue::Nil());
  }
}


/*
** set functions (stack -> Lua)
*/


void lua_setglobal (LuaThread *L, const char *var) {
  THREAD_CHECK(L);
  L->stack_.checkArgs(1);

  LuaValue globals = thread_G->l_registry.getTable()->get(LuaValue(LUA_RIDX_GLOBALS));

  LuaString* s = thread_G->strings_->Create(var);
  L->stack_.push(LuaValue(s));

  luaV_settable(L, &globals, L->stack_.top_ - 1, L->stack_.top_ - 2);
  L->stack_.top_ -= 2;  /* pop value and key */
}


void lua_settable (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  L->stack_.checkArgs(2);
  t = index2addr_checked(L, idx);
  luaV_settable(L, t, L->stack_.top_ - 2, L->stack_.top_ - 1);
  L->stack_.top_ -= 2;  /* pop index and value */
}


void lua_setfield (LuaThread *L, int idx, const char *k) {
  THREAD_CHECK(L);
  StkId t;
  L->stack_.checkArgs(1);
  t = index2addr_checked(L, idx);

  LuaString* s = thread_G->strings_->Create(k);
  L->stack_.push(LuaValue(s));

  luaV_settable(L, t, L->stack_.top_ - 1, L->stack_.top_ - 2);
  L->stack_.top_ -= 2;  /* pop value and key */
}


void lua_rawset (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  L->stack_.checkArgs(2);
  t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");

  t->getTable()->set(L->stack_.top_[-2], L->stack_.top_[-1]);

  luaC_barrierback(t->getObject(), L->stack_.top_[-1]);
  luaC_barrierback(t->getObject(), L->stack_.top_[-2]);
  L->stack_.top_ -= 2;
}


void lua_rawseti (LuaThread *L, int idx, int n) {
  THREAD_CHECK(L);
  StkId t;
  L->stack_.checkArgs(1);
  t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");

  t->getTable()->set(LuaValue(n), L->stack_.top_[-1]);

  luaC_barrierback(t->getObject(), L->stack_.top_[-1]);
  L->stack_.pop();
}


void lua_rawsetp (LuaThread *L, int idx, const void *p) {
  THREAD_CHECK(L);
  StkId t;
  LuaValue k;
  L->stack_.checkArgs(1);
  t = index2addr(L, idx);
  assert(t);
  api_check(t->isTable(), "table expected");
  k = LuaValue::LightUserdata((void*)p);

  t->getTable()->set(k, L->stack_.top_[-1]);

  luaC_barrierback(t->getObject(), L->stack_.top_[-1]);
  L->stack_.pop();
}


int lua_setmetatable (LuaThread *L, int objindex) {
  THREAD_CHECK(L);
  LuaValue *obj;
  LuaTable *mt;
  L->stack_.checkArgs(1);
  obj = index2addr_checked(L, objindex);
  if (L->stack_.top_[-1].isNil()) {
    mt = NULL;
  }
  else {
    api_check(L->stack_.top_[-1].isTable(), "table expected");
    mt = L->stack_.top_[-1].getTable();
  }

  // This is a little weird, tables and userdata get their
  // own metatables, but trying to set a metatable for anything
  // else overrides the _global_ metatable.

  switch (obj->type()) {
    case LUA_TTABLE:
      {
        obj->getTable()->metatable = mt;
        if (mt) {
          luaC_barrierback(obj->getObject(), LuaValue(mt));
          luaC_checkfinalizer(obj->getObject(), mt);
        }
        break;
      }
    case LUA_TBLOB:
      {
        obj->getUserdata()->metatable_ = mt;
        if (mt) {
          luaC_barrier(obj->getUserdata(), LuaValue(mt));
          luaC_checkfinalizer(obj->getObject(), mt);
        }
        break;
      }
    default: 
      {
        thread_G->base_metatables_[obj->type()] = mt;
        break;
      }
  }

  L->stack_.pop();
  return 1;
}


void lua_setuservalue (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId o;
  L->stack_.checkArgs(1);
  o = index2addr_checked(L, idx);
  api_check(o->isUserdata(), "userdata expected");
  if (L->stack_.top_[-1].isNil())
    o->getUserdata()->env_ = NULL;
  else {
    api_check(L->stack_.top_[-1].isTable(), "table expected");
    o->getUserdata()->env_ = L->stack_.top_[-1].getTable();
    luaC_barrier(o->getObject(), L->stack_.top_[-1]);
  }
  L->stack_.pop();
}


/*
** `load' and `call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check((nr) == LUA_MULTRET || (L->stack_.callinfo_->getTop() - L->stack_.top_ >= (nr) - (na)), \
	"results from function overflow current stack size")


int lua_getctx (LuaThread *L, int *ctx) {
  THREAD_CHECK(L);
  if (L->stack_.callinfo_->callstatus & CIST_YIELDED) {
    if (ctx) *ctx = L->stack_.callinfo_->continuation_context_;
    return L->stack_.callinfo_->status;
  }
  else return LUA_OK;
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
    StkId func = L->stack_.top_ - (nargs+1);
    luaD_call(L, func, nresults, 1);  /* do the call */
  }
  else {
    /* no continuation or no yieldable */
    StkId func = L->stack_.top_ - (nargs+1);
    luaD_call(L, func, nresults, 0);  /* just do the call */
  }
  adjustresults(L, nresults);
}



/*
** Execute a protected call.
*/

int lua_pcall (LuaThread *L, int nargs, int nresults, int errfunc) {
  THREAD_CHECK(L);

  if(nresults == LUA_MULTRET) {
    int b = 0;
    b++;
  }

  L->stack_.checkArgs(nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);

  ptrdiff_t errfunc_index = 0;
  if (errfunc) {
    StkId o = index2addr_checked(L, errfunc);
    errfunc_index = savestack(L, o);
  }

  StkId func = L->stack_.top_ - (nargs+1);

  // Save the parts of the execution state that will get modified by the call
  LuaExecutionState s = L->saveState(func);

  L->errfunc = errfunc_index;

  LuaResult status = LUA_OK;
  try {
    L->nonyieldable_count_++;
    if (!luaD_precall(L, func, nresults)) {
      luaV_execute(L);
    }

    // IMPORTANT - If we ran out of memory during this pcall, make sure the error
    // is thrown before we leave this try() block - otherwise the exception will get
    // thrown in the parent context, which may not be what the user expects.

    l_memcontrol.checkLimit();
  }
  catch(LuaResult error) {
    status = error;
  }

  L->restoreState(s, status, nresults);
  return status;
}

int lua_pcallk (LuaThread *L, int nargs, int nresults, int errfunc,
                        int ctx, LuaCallback k) {
  THREAD_CHECK(L);

  // If there's no continuation or we can't yield, do a non-yielding call.
  if (k == NULL || L->nonyieldable_count_ > 0) {
    return lua_pcall(L,nargs,nresults,errfunc);
  }

  api_check(k == NULL || !L->stack_.callinfo_->isLua(), "cannot use continuations inside hooks");
  L->stack_.checkArgs(nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);

  ptrdiff_t errfunc_index = 0;
  if (errfunc) {
    StkId o = index2addr_checked(L, errfunc);
    errfunc_index = savestack(L, o);
  }

  StkId func = L->stack_.top_ - (nargs+1);  /* function to be called */

  // prepare continuation (call is already protected by 'resume')
  LuaStackFrame *ci = L->stack_.callinfo_;
  ci->continuation_ = k;  /* save continuation */
  ci->continuation_context_ = ctx;  /* save context */

  /* save information for error recovery */
  ci->old_func_ = savestack(L, func);
  ci->old_allowhook = L->allowhook;
  ci->old_errfunc = L->errfunc;

  L->errfunc = errfunc_index;

  ci->callstatus |= CIST_YPCALL;
  luaD_call(L, func, nresults, 1);  /* do the call */
  ci->callstatus &= ~CIST_YPCALL;

  L->errfunc = ci->old_errfunc;
  adjustresults(L, nresults);
  return LUA_OK;
}


int lua_load (LuaThread *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  THREAD_CHECK(L);

  ZIO z;
  int status;
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LuaClosure *f = L->stack_.top_[-1].getLClosure();  /* get newly created function */
    if (f->nupvalues == 1) {  /* does it have one upvalue? */
      /* get global table from registry */
      LuaValue globals = thread_G->l_registry.getTable()->get(LuaValue(LUA_RIDX_GLOBALS));
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      *f->ppupvals_[0]->v = globals;
      luaC_barrier(f->ppupvals_[0], globals);
    }
  }
  return status;
}


int lua_dump (LuaThread *L, lua_Writer writer, void *data) {
  THREAD_CHECK(L);
  int status;
  LuaValue *o;
  L->stack_.checkArgs(1);
  o = L->stack_.top_ - 1;
  if (o->isLClosure())
    status = luaU_dump(L, o->getLClosure()->proto_, writer, data, 0);
  else
    status = 1;
  return status;
}


/*
** Garbage-collection function
*/

int lua_gc (LuaThread *L, int what, int data) {
  THREAD_CHECK(L);
  int res = 0;
  LuaVM *g;
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      g->setGCDebt(0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(g->getTotalBytes() >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(g->getTotalBytes() & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      if (g->gckind == KGC_GEN) {  /* generational mode? */
        res = (g->lastmajormem == 0);  /* 1 if will do major collection */
        luaC_forcestep();  /* do a single step */
      }
      else {
        while (data-- >= 0) {
          luaC_forcestep();
          if (g->gcstate == GCSpause) {  /* end of cycle? */
            res = 1;  /* signal it */
            break;
          }
        }
      }
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETMAJORINC: {
      res = g->gcmajorinc;
      g->gcmajorinc = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    case LUA_GCGEN: {  /* change collector to generational mode */
      luaC_changemode(L, KGC_GEN);
      break;
    }
    case LUA_GCINC: {  /* change collector to incremental mode */
      luaC_changemode(L, KGC_NORMAL);
      break;
    }
    default: res = -1;  /* invalid option */
  }
  return res;
}



/*
** miscellaneous functions
*/


int lua_error (LuaThread *L) {
  THREAD_CHECK(L);
  L->stack_.checkArgs(1);
  luaG_errormsg();
}

int lua_next (LuaThread* L, int idx) {

  LuaTable* t = L->stack_.at(idx).getTable();

  LuaValue key = L->stack_.pop();

  int start = -1;
  if(!key.isNil()) {
    bool found = t->keyToTableIndex(key,start);
    if(!found) {
      luaG_runerror("invalid key to 'next'");
    }
  }

  for(int cursor = start+1; cursor < t->getTableIndexSize(); cursor++) {
    LuaValue key, val;
    if(t->tableIndexToKeyVal(cursor,key,val) && !val.isNil()) {
      L->stack_.push(key);
      L->stack_.push(val);
      return 1;
    }
  }

  return 0;
}

void lua_concat (LuaThread *L, int n) {
  THREAD_CHECK(L);
  L->stack_.checkArgs(n);
  if (n >= 2) {
    luaV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    LuaString* s = thread_G->strings_->Create("", 0);
    L->stack_.push(LuaValue(s));
  }
  /* else n == 1; nothing to do */
}


void lua_len (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  assert(t);

  // objlen puts result on stack instead of just returning it...
  luaV_objlen(L, L->stack_.top_, t);
  L->stack_.top_++;

  api_check(L->stack_.top_ <= L->stack_.callinfo_->getTop(), "stack overflow");
}


void *lua_newuserdata (LuaThread *L, size_t size) {
  THREAD_CHECK(L);

  if(!l_memcontrol.canAlloc(size)) {
    throwError(LUA_ERRMEM);
  }

  LuaBlob* u = new LuaBlob(size);
  L->stack_.push(LuaValue(u));

  return u->buf_;
}



static const char *aux_upvalue (StkId fi, int n, LuaValue **val,
                                LuaObject **owner) {
  switch (fi->type()) {
    case LUA_TCCL: {  /* C closure */
      LuaClosure *f = fi->getCClosure();
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->pupvals_[n-1];
      if (owner) *owner = f;
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      LuaClosure *f = fi->getLClosure();
      LuaString *name;
      LuaProto *p = f->proto_;
      if (!(1 <= n && n <= (int)p->upvalues.size())) return NULL;
      *val = f->ppupvals_[n-1]->v;
      if (owner) *owner = f->ppupvals_[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "" : name->c_str();
    }
    default: return NULL;  /* not a closure */
  }
}


const char *lua_getupvalue (LuaThread *L, int funcindex, int n) {
  THREAD_CHECK(L);
  const char *name;
  LuaValue *val = NULL;  /* to avoid warnings */
  LuaValue* val2 = index2addr(L, funcindex);
  if(val2 == NULL) return NULL;
  name = aux_upvalue(val2, n, &val, NULL);
  if (name) {
    L->stack_.push(*val);
  }
  return name;
}


const char *lua_setupvalue (LuaThread *L, int funcindex, int n) {
  THREAD_CHECK(L);
  const char *name;
  LuaValue *val = NULL;  /* to avoid warnings */
  LuaObject *owner = NULL;  /* to avoid warnings */
  StkId fi;
  fi = index2addr(L, funcindex);
  assert(fi);
  L->stack_.checkArgs(1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    *val = L->stack_.pop();
    luaC_barrier(owner, *val);
  }
  return name;
}


static LuaUpvalue **getupvalref (LuaThread *L, int fidx, int n, LuaClosure **pf) {
  THREAD_CHECK(L);
  LuaClosure *f;
  StkId fi = index2addr(L, fidx);
  assert(fi);
  api_check(fi->isLClosure(), "Lua function expected");
  f = fi->getLClosure();
  api_check((1 <= n && n <= (int)f->proto_->upvalues.size()), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->ppupvals_[n - 1];  /* get its upvalue pointer */
}


void *lua_upvalueid (LuaThread *L, int fidx, int n) {
  THREAD_CHECK(L);
  StkId fi = index2addr(L, fidx);
  assert(fi);
  switch (fi->type()) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      LuaClosure *f = fi->getCClosure();
      api_check(1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->pupvals_[n - 1];
    }
    default: {
      api_check(0, "closure expected");
      return NULL;
    }
  }
}


void lua_upvaluejoin (LuaThread *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  THREAD_CHECK(L);
  LuaClosure *f1;
  LuaUpvalue **up1 = getupvalref(L, fidx1, n1, &f1);
  LuaUpvalue **up2 = getupvalref(L, fidx2, n2, NULL);
  *up1 = *up2;
  luaC_barrier(f1, LuaValue(*up2));
}

