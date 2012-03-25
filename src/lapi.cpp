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
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, luaO_nilobject)

/* corresponding test */
#define isvalid(o)	((o) != luaO_nilobject)

#define api_checkvalidindex(i)  api_check(isvalid(i), "invalid index")

void api_checknelems(lua_State* L, int n) {
  api_check((n) < (L->top - L->ci_->func), "not enough elements in the stack");
}

// Positive stack indices are indexed from the current call frame.
// The first item in a call frame is the closure for the current function.
// Negative stack indices are indexed from the stack top.
// Negative indices less than or equal to LUA_REGISTRYINDEX are special.

static TValue *index2addr (lua_State *L, int idx) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  if (idx > 0) {
    TValue *o = ci->func + idx;
    if (o >= L->top) {
      return NONVALIDVALUE;
    }
    else return o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return L->top + idx;
  }

  if (idx == LUA_REGISTRYINDEX) {
    return &thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (ci->func->isLightCFunc()) {
    return NONVALIDVALUE;
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  Closure* func = ci->func->getCClosure();
  if(idx < func->nupvalues) {
    return &func->pupvals_[idx];
  }

  // Invalid stack index.
  return NONVALIDVALUE;
}


/*
** to be called by 'lua_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
static void growstack (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  int size = *(int *)ud;
  L->growstack(size);
}


int lua_checkstack (lua_State *L, int size) {
  THREAD_CHECK(L);
  int res;
  CallInfo *ci = L->ci_;
  if (L->stack_last - L->top > size)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    int inuse = cast_int(L->top - L->stack.begin()) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - size)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      res = (luaD_rawrunprotected(L, &growstack, &size) == LUA_OK);
  }
  if (res && ci->top < L->top + size)
    ci->top = L->top + size;  /* adjust frame top */
  return res;
}


void lua_xmove (lua_State *from, lua_State *to, int n) {
  THREAD_CHECK(from);
  int i;
  if (from == to) return;
  api_checknelems(from, n);
  api_check(G(from) == G(to), "moving among independent states");
  api_check(to->ci_->top - to->top >= n, "not enough elements to move");
  from->top -= n;
  {
    THREAD_CHANGE(to);
    for (i = 0; i < n; i++) {
      setobj(to->top++, from->top + i);
    }
  }
}


lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  THREAD_CHECK(L);
  lua_CFunction old;
  old = G(L)->panic;
  G(L)->panic = panicf;
  return old;
}


const lua_Number *lua_version (lua_State *L) {
  //THREAD_CHECK(L);
  static const lua_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
int lua_absindex (lua_State *L, int idx) {
  THREAD_CHECK(L);
  return (idx > 0 || idx <= LUA_REGISTRYINDEX)
         ? idx
         : cast_int(L->top - L->ci_->func + idx);
}


int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci_->func + 1));
}


void lua_settop (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId func = L->ci_->func;
  if (idx >= 0) {
    api_check(idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx) {
      setnilvalue(L->top);
      L->top++;
    }
    L->top = (func + 1) + idx;
  }
  else {
    api_check(-(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* `subtract' index (index is negative) */
  }
}


void lua_remove (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId p;
  p = index2addr(L, idx);
  api_checkvalidindex(p);
  while (++p < L->top) setobj(p-1, p);
  L->top--;
}


void lua_insert (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId p;
  StkId q;
  p = index2addr(L, idx);
  api_checkvalidindex(p);
  for (q = L->top; q>p; q--) setobj(q, q-1);
  setobj(p, L->top);
}


static void moveto (lua_State *L, TValue *fr, int idx) {
  THREAD_CHECK(L);
  TValue *to = index2addr(L, idx);
  api_checkvalidindex(to);
  setobj(to, fr);
  if (idx < LUA_REGISTRYINDEX)  /* function upvalue? */
    luaC_barrier(clCvalue(L->ci_->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
}


void lua_replace (lua_State *L, int idx) {
  THREAD_CHECK(L);
  api_checknelems(L, 1);
  moveto(L, L->top - 1, idx);
  L->top--;
}


void lua_copy (lua_State *L, int fromidx, int toidx) {
  THREAD_CHECK(L);
  TValue *fr;
  fr = index2addr(L, fromidx);
  api_checkvalidindex(fr);
  moveto(L, fr, toidx);
}


void lua_pushvalue (lua_State *L, int idx) {
  THREAD_CHECK(L);
  setobj(L->top, index2addr(L, idx));
  api_incr_top(L);
}



/*
** access functions (stack -> C)
*/


int lua_type (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttypenv(o) : LUA_TNONE);
}


const char *lua_typename (lua_State *L, int t) {
  THREAD_CHECK(L);
  UNUSED(L);
  return ttypename(t);
}


int lua_iscfunction (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


int lua_isnumber (lua_State *L, int idx) {
  THREAD_CHECK(L);
  TValue n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}


int lua_isstring (lua_State *L, int idx) {
  THREAD_CHECK(L);
  int t = lua_type(L, idx);
  return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


int lua_isuserdata (lua_State *L, int idx) {
  THREAD_CHECK(L);
  const TValue *o = index2addr(L, idx);
  return (ttisuserdata(o) || ttislightuserdata(o));
}


int lua_rawequal (lua_State *L, int index1, int index2) {
  THREAD_CHECK(L);
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  return (isvalid(o1) && isvalid(o2)) ? luaV_rawequalobj(o1, o2) : 0;
}


void  lua_arith (lua_State *L, int op) {
  THREAD_CHECK(L);
  StkId o1;  /* 1st operand */
  StkId o2;  /* 2nd operand */
  if (op != LUA_OPUNM) /* all other operations expect two operands */
    api_checknelems(L, 2);
  else {  /* for unary minus, add fake 2nd operand */
    api_checknelems(L, 1);
    setobj(L->top, L->top - 1);
    L->top++;
  }
  o1 = L->top - 2;
  o2 = L->top - 1;
  if (ttisnumber(o1) && ttisnumber(o2)) {
    changenvalue(o1, luaO_arith(op, nvalue(o1), nvalue(o2)));
  }
  else
    luaV_arith(L, o1, o1, o2, cast(TMS, op - LUA_OPADD + TM_ADD));
  L->top--;
}


int lua_compare (lua_State *L, int index1, int index2, int op) {
  THREAD_CHECK(L);
  StkId o1, o2;
  int i = 0;
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(0, "invalid option");
    }
  }
  return i;
}


lua_Number lua_tonumberx (lua_State *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    if (isnum) *isnum = 1;
    return nvalue(o);
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


lua_Integer lua_tointegerx (lua_State *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    lua_Integer res;
    lua_Number num = nvalue(o);
    lua_number2integer(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


lua_Unsigned lua_tounsignedx (lua_State *L, int idx, int *isnum) {
  THREAD_CHECK(L);
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    lua_Unsigned res;
    lua_Number num = nvalue(o);
    lua_number2unsigned(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


int lua_toboolean (lua_State *L, int idx) {
  THREAD_CHECK(L);
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}


const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    if (!luaV_tostring(L, o)) {  /* conversion failed? */
      if (len != NULL) *len = 0;
      return NULL;
    }
    luaC_checkGC();
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
  }
  if (len != NULL) *len = tsvalue(o)->getLen();
  return tsvalue(o)->c_str();
}


size_t lua_rawlen (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  switch (o->basetype()) {
    case LUA_TSTRING: return tsvalue(o)->getLen();
    case LUA_TUSERDATA: return uvalue(o)->len_;
    case LUA_TTABLE: return luaH_getn(hvalue(o));
    default: return 0;
  }
}


lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


void *lua_touserdata (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  switch (ttypenv(o)) {
    case LUA_TUSERDATA: return (uvalue(o)->buf_);
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


lua_State *lua_tothread (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


const void *lua_topointer (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TTABLE: return hvalue(o);
    case LUA_TLCL: return clLvalue(o);
    case LUA_TCCL: return clCvalue(o);
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case LUA_TTHREAD: return thvalue(o);
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
      return lua_touserdata(L, idx);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


void lua_pushnil (lua_State *L) {
  THREAD_CHECK(L);
  setnilvalue(L->top);
  api_incr_top(L);
}


void lua_pushnumber (lua_State *L, lua_Number n) {
  THREAD_CHECK(L);
  setnvalue(L->top, n);
  luai_checknum(L, L->top,
    luaG_runerror("C API - attempt to push a signaling NaN"));
  api_incr_top(L);
}


void lua_pushinteger (lua_State *L, lua_Integer n) {
  THREAD_CHECK(L);
  setnvalue(L->top, cast_num(n));
  api_incr_top(L);
}


void lua_pushunsigned (lua_State *L, lua_Unsigned u) {
  THREAD_CHECK(L);
  lua_Number n;
  n = lua_unsigned2number(u);
  setnvalue(L->top, n);
  api_incr_top(L);
}


const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  THREAD_CHECK(L);
  TString *ts;
  luaC_checkGC();
  ts = luaS_newlstr(s, len);
  L->top[0] = ts;
  api_incr_top(L);
  return ts->c_str();
}


const char *lua_pushstring (lua_State *L, const char *s) {
  THREAD_CHECK(L);
  if (s == NULL) {
    lua_pushnil(L);
    return NULL;
  }
  else {
    TString *ts;
    luaC_checkGC();
    ts = luaS_new(s);
    L->top[0] = ts;
    api_incr_top(L);
    return ts->c_str();
  }
}


const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  THREAD_CHECK(L);
  const char *ret;
  luaC_checkGC();
  ret = luaO_pushvfstring(fmt, argp);
  return ret;
}


const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  THREAD_CHECK(L);
  const char *ret;
  va_list argp;
  luaC_checkGC();
  va_start(argp, fmt);
  ret = luaO_pushvfstring(fmt, argp);
  va_end(argp);
  return ret;
}


void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  THREAD_CHECK(L);
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    Closure *cl;
    api_checknelems(L, n);
    api_check(n <= MAXUPVAL, "upvalue index too large");
    luaC_checkGC();
    cl = luaF_newCclosure(n);
    if(cl == NULL) luaD_throw(LUA_ERRMEM);
    cl->f = fn;
    L->top -= n;
    while (n--)
      setobj(&cl->pupvals_[n], L->top + n);
    setclCvalue(L, L->top, cl);
  }
  api_incr_top(L);
}


void lua_pushboolean (lua_State *L, int b) {
  THREAD_CHECK(L);
  L->top[0] = (b ? true : false);
  api_incr_top(L);
}


void lua_pushlightuserdata (lua_State *L, void *p) {
  THREAD_CHECK(L);
  setpvalue(L->top, p);
  api_incr_top(L);
}


int lua_pushthread (lua_State *L) {
  THREAD_CHECK(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/


void lua_getglobal (lua_State *L, const char *var) {
  THREAD_CHECK(L);
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  L->top[0] = luaS_new(var);
  L->top++;
  luaV_gettable(L, gt, L->top - 1, L->top - 1);
}


void lua_gettable (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  api_checkvalidindex(t);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
}


void lua_getfield (lua_State *L, int idx, const char *k) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  api_checkvalidindex(t);
  L->top[0] = luaS_new(k);
  api_incr_top(L);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
}


void lua_rawget (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setobj(L->top - 1, luaH_get(hvalue(t), L->top - 1));
}


void lua_rawgeti (lua_State *L, int idx, int n) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setobj(L->top, luaH_getint(hvalue(t), n));
  api_incr_top(L);
}


void lua_rawgetp (lua_State *L, int idx, const void *p) {
  THREAD_CHECK(L);
  StkId t;
  TValue k;
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj(L->top, luaH_get(hvalue(t), &k));
  api_incr_top(L);
}


void lua_createtable (lua_State *L, int narray, int nrec) {
  THREAD_CHECK(L);
  Table *t;
  luaC_checkGC();
  t = new Table();
  if(t == NULL) luaD_throw(LUA_ERRMEM);
  t->linkGC(getGlobalGCHead());
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    luaH_resize(t, narray, nrec);
}


int lua_getmetatable (lua_State *L, int objindex) {
  THREAD_CHECK(L);
  const TValue *obj;
  Table *mt = NULL;
  int res;
  obj = index2addr(L, objindex);
  switch (ttypenv(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable_;
      break;
    default:
      mt = G(L)->mt[ttypenv(obj)];
      break;
  }
  if (mt == NULL)
    res = 0;
  else {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  return res;
}


void lua_getuservalue (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o;
  o = index2addr(L, idx);
  api_checkvalidindex(o);
  api_check(ttisuserdata(o), "userdata expected");
  if (uvalue(o)->env_) {
    sethvalue(L, L->top, uvalue(o)->env_);
  } else
    setnilvalue(L->top);
  api_incr_top(L);
}


/*
** set functions (stack -> Lua)
*/


void lua_setglobal (lua_State *L, const char *var) {
  THREAD_CHECK(L);
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  api_checknelems(L, 1);
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  L->top[0] = luaS_new(var);
  L->top++;
  luaV_settable(L, gt, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
}


void lua_settable (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  api_checkvalidindex(t);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
}


void lua_setfield (lua_State *L, int idx, const char *k) {
  THREAD_CHECK(L);
  StkId t;
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_checkvalidindex(t);
  L->top[0] = luaS_new(k);
  L->top++;
  luaV_settable(L, t, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
}


void lua_rawset (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setobj(luaH_set(hvalue(t), L->top-2), L->top-1);
  // invalidate TM cache
  hvalue(t)->flags = 0;
  luaC_barrierback(gcvalue(t), L->top-1);
  L->top -= 2;
}


void lua_rawseti (lua_State *L, int idx, int n) {
  THREAD_CHECK(L);
  StkId t;
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  luaH_setint(hvalue(t), n, L->top - 1);
  luaC_barrierback(gcvalue(t), L->top-1);
  L->top--;
}


void lua_rawsetp (lua_State *L, int idx, const void *p) {
  THREAD_CHECK(L);
  StkId t;
  TValue k;
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj(luaH_set(hvalue(t), &k), L->top - 1);
  luaC_barrierback(gcvalue(t), L->top - 1);
  L->top--;
}


int lua_setmetatable (lua_State *L, int objindex) {
  THREAD_CHECK(L);
  TValue *obj;
  Table *mt;
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  api_checkvalidindex(obj);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttypenv(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt)
        luaC_objbarrierback(L, gcvalue(obj), mt);
        luaC_checkfinalizer(gcvalue(obj), mt);
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable_ = mt;
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        luaC_checkfinalizer(gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttypenv(obj)] = mt;
      break;
    }
  }
  L->top--;
  return 1;
}


void lua_setuservalue (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId o;
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_checkvalidindex(o);
  api_check(ttisuserdata(o), "userdata expected");
  if (ttisnil(L->top - 1))
    uvalue(o)->env_ = NULL;
  else {
    api_check(ttistable(L->top - 1), "table expected");
    uvalue(o)->env_ = hvalue(L->top - 1);
    luaC_objbarrier(L, gcvalue(o), hvalue(L->top - 1));
  }
  L->top--;
}


/*
** `load' and `call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check((nr) == LUA_MULTRET || (L->ci_->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")


int lua_getctx (lua_State *L, int *ctx) {
  THREAD_CHECK(L);
  if (L->ci_->callstatus & CIST_YIELDED) {
    if (ctx) *ctx = L->ci_->ctx;
    return L->ci_->status;
  }
  else return LUA_OK;
}


void lua_callk (lua_State *L, int nargs, int nresults, int ctx,
                        lua_CFunction k) {
  THREAD_CHECK(L);
  StkId func;
  api_check(k == NULL || !isLua(L->ci_),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && L->nny == 0) {  /* need to prepare continuation? */
    L->ci_->k = k;  /* save continuation */
    L->ci_->ctx = ctx;  /* save context */
    luaD_call(L, func, nresults, 1);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    luaD_call(L, func, nresults, 0);  /* just do the call */
  adjustresults(L, nresults);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to `f_call' */
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_call(L, c->func, c->nresults, 0);
}

int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        int ctx, lua_CFunction k) {
  THREAD_CHECK(L);

  struct CallS c;
  int status;
  ptrdiff_t func;
  api_check(k == NULL || !isLua(L->ci_),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkvalidindex(o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || L->nny > 0) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci_;
    ci->k = k;  /* save continuation */
    ci->ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);
    ci->old_allowhook = L->allowhook;
    ci->old_errfunc = L->errfunc;
    L->errfunc = func;
    /* mark that function may do error recovery */
    ci->callstatus |= CIST_YPCALL;
    luaD_call(L, c.func, nresults, 1);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  return status;
}


int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  THREAD_CHECK(L);

  ZIO z;
  int status;
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    Closure *f = clLvalue(L->top - 1);  /* get newly created function */
    if (f->nupvalues == 1) {  /* does it have one upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(f->ppupvals_[0]->v, gt);
      luaC_barrier(f->ppupvals_[0], gt);
    }
  }
  return status;
}


int lua_dump (lua_State *L, lua_Writer writer, void *data) {
  THREAD_CHECK(L);
  int status;
  TValue *o;
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, 0);
  else
    status = 1;
  return status;
}


int  lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/

int lua_gc (lua_State *L, int what, int data) {
  THREAD_CHECK(L);
  int res = 0;
  global_State *g;
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
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


int lua_error (lua_State *L) {
  THREAD_CHECK(L);
  api_checknelems(L, 1);
  luaG_errormsg();
  return 0;  /* to avoid warnings */
}

int lua_next (lua_State* L, int idx) {

  Table* t = L->at(idx);

  TValue key = L->pop();

  int start = -1;
  if(!key.isNil()) {
    bool found = t->keyToTableIndex(key,start);
    if(!found) {
      luaG_runerror("invalid key to 'next'");
      return 0;
    }
  }

  for(int cursor = start+1; cursor < t->getTableIndexSize(); cursor++) {
    TValue key, val;
    if(t->tableIndexToKeyVal(cursor,key,val) && !val.isNil()) {
      L->push(key);
      L->push(val);
      return 1;
    }
  }

  return 0;
}

void lua_concat (lua_State *L, int n) {
  THREAD_CHECK(L);
  api_checknelems(L, n);
  if (n >= 2) {
    luaC_checkGC();
    luaV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    L->top[0] = luaS_newlstr("", 0);
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
}


void lua_len (lua_State *L, int idx) {
  THREAD_CHECK(L);
  StkId t;
  t = index2addr(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
}


void *lua_newuserdata (lua_State *L, size_t size) {
  THREAD_CHECK(L);
  Udata *u;
  luaC_checkGC();
  u = luaS_newudata(size, NULL);
  if(u == NULL) luaD_throw(LUA_ERRMEM);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  return u->buf_;
}



static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                LuaObject **owner) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      Closure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->pupvals_[n-1];
      if (owner) *owner = f;
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      Closure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= (int)p->upvalues.size())) return NULL;
      *val = f->ppupvals_[n-1]->v;
      if (owner) *owner = f->ppupvals_[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "" : name->c_str();
    }
    default: return NULL;  /* not a closure */
  }
}


const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  THREAD_CHECK(L);
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL);
  if (name) {
    setobj(L->top, val);
    api_incr_top(L);
  }
  return name;
}


const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  THREAD_CHECK(L);
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  LuaObject *owner = NULL;  /* to avoid warnings */
  StkId fi;
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top--;
    setobj(val, L->top);
    luaC_barrier(owner, L->top);
  }
  return name;
}


static UpVal **getupvalref (lua_State *L, int fidx, int n, Closure **pf) {
  THREAD_CHECK(L);
  Closure *f;
  StkId fi = index2addr(L, fidx);
  api_check(ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  api_check((1 <= n && n <= (int)f->p->upvalues.size()), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->ppupvals_[n - 1];  /* get its upvalue pointer */
}


void *lua_upvalueid (lua_State *L, int fidx, int n) {
  THREAD_CHECK(L);
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      Closure *f = clCvalue(fi);
      api_check(1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->pupvals_[n - 1];
    }
    default: {
      api_check(0, "closure expected");
      return NULL;
    }
  }
}


void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  THREAD_CHECK(L);
  Closure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  *up1 = *up2;
  luaC_objbarrier(L, f1, *up2);
}

