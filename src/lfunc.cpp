/*
** $Id: lfunc.c,v 2.27 2010/06/30 14:11:17 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"
#include "LuaUpval.h"

#include <stddef.h>

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



Closure *luaF_newCclosure (int n) {
  LuaObject* o = NULL;
  TValue* b = NULL;

  try {
    b = (TValue*)luaM_alloc(n * sizeof(TValue));
    if(b == NULL) luaD_throw(LUA_ERRMEM);
    o = luaC_newobj(LUA_TFUNCTION, sizeof(Closure), NULL);
    if(o == NULL) luaD_throw(LUA_ERRMEM);
  } catch(...) {
    luaM_delobject(o);
    luaM_free(b);
    throw;
  }
  LuaObject::instanceCounts[LUA_TFUNCTION]++;

  Closure *c = gco2cl(o);
  c->isC = 1;
  c->nupvalues = cast_byte(n);
  c->pupvals_ = b;
  c->ppupvals_ = NULL;
  return c;
}


Closure *luaF_newLclosure (Proto *p) {
  int n = (int)p->upvalues.size();

  LuaObject* o = NULL;
  UpVal** b = NULL;

  try {
    b = (UpVal**)luaM_alloc(n * sizeof(TValue*));
    if(b == NULL) luaD_throw(LUA_ERRMEM);
    o = luaC_newobj(LUA_TFUNCTION, sizeof(Closure), NULL);
    if(o == NULL) luaD_throw(LUA_ERRMEM);
  } catch(...) {
    luaM_delobject(o);
    luaM_free(b);
    throw;
  }
  LuaObject::instanceCounts[LUA_TFUNCTION]++;

  Closure *c = gco2cl(o);
  c->isC = 0;
  c->p = p;
  c->nupvalues = cast_byte(n);
  c->pupvals_ = NULL;
  c->ppupvals_ = b;
  while (n--) c->ppupvals_[n] = NULL;
  return c;
}


UpVal *luaF_newupval () {
  UpVal *uv = new UpVal(NULL);
  if(uv == NULL) luaD_throw(LUA_ERRMEM);
  uv->v = &uv->value;
  setnilvalue(uv->v);
  return uv;
}


UpVal *luaF_findupval (StkId level) {
  global_State *g = thread_G;
  LuaObject **pp = &thread_L->openupval;
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
    LuaObject *o = obj2gco(p);
    assert(p->v != &p->value);
    if (p->v == level) {  /* found a corresponding upvalue? */
      if (isdead(o))  /* is it dead? */
        changewhite(o);  /* resurrect it */
      return p;
    }
    resetoldbit(o);  /* may create a newer upval after this one */
    pp = &(p->next);
  }
  /* not found: create a new one */
  uv = new UpVal(pp);
  if(uv == NULL) luaD_throw(LUA_ERRMEM);
  uv->v = level;  /* current value lives in the stack */
  uv->uprev = &g->uvhead;  /* double link it in `uvhead' list */
  uv->unext = g->uvhead.unext;
  uv->unext->uprev = uv;
  g->uvhead.unext = uv;
  assert(uv->unext->uprev == uv && uv->uprev->unext == uv);
  return uv;
}


static void unlinkupval (UpVal *uv) {
  assert(uv->unext->uprev == uv && uv->uprev->unext == uv);
  uv->unext->uprev = uv->uprev;  /* remove from `uvhead' list */
  uv->uprev->unext = uv->unext;
}


void luaF_freeupval (UpVal *uv) {
  if (uv->v != &uv->value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  delete uv;  /* free upvalue */
}


void luaF_close (StkId level) {
  UpVal *uv;
  lua_State* L = thread_L;
  global_State *g = thread_G;
  while (L->openupval != NULL && (uv = gco2uv(L->openupval))->v >= level) {
    LuaObject *o = obj2gco(uv);
    assert(!isblack(o) && uv->v != &uv->value);
    L->openupval = uv->next;  /* remove from `open' list */
    if (isdead(o))
      luaF_freeupval(uv);  /* free upvalue */
    else {
      unlinkupval(uv);  /* remove upvalue from 'uvhead' list */
      setobj(&uv->value, uv->v);  /* move value to upvalue slot */
      uv->v = &uv->value;  /* now current value lives here */
      gch(o)->next = g->allgc;  /* link upvalue into 'allgc' list */
      g->allgc = o;
      luaC_checkupvalcolor(g, uv);
    }
  }
}


void luaF_freeclosure (Closure *c) {
  luaM_free(c->pupvals_);
  luaM_free(c->ppupvals_);
  luaM_delobject(c);
  LuaObject::instanceCounts[LUA_TFUNCTION]--;
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns NULL if not found.
*/
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<(int)f->locvars.size() && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return f->locvars[i].varname->c_str();
    }
  }
  return NULL;  /* not found */
}

