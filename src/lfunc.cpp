/*
** $Id: lfunc.c,v 2.27 2010/06/30 14:11:17 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
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
    o = luaC_newobj(LUA_TFUNCTION, sizeof(Closure), NULL);
  } catch(...) {
    luaM_delobject(o);
    luaM_free(b);
    throw;
  }

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
    o = luaC_newobj(LUA_TFUNCTION, sizeof(Closure), NULL);
  } catch(...) {
    luaM_delobject(o);
    luaM_free(b);
    throw;
  }

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
  LuaObject *o = luaC_newobj(LUA_TUPVAL, sizeof(UpVal), NULL);
  UpVal *uv = gco2uv(o);
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
  LuaObject* o = luaC_newobj(LUA_TUPVAL, sizeof(UpVal), pp);
  uv = gco2uv(o);
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
  luaM_delobject(uv);  /* free upvalue */
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


Proto *luaF_newproto () {
  LuaObject* o = luaC_newobj(LUA_TPROTO, sizeof(Proto), NULL);
  Proto* f = gco2p(o);
  f->constants.init();
  f->p.init();
  f->code.init();
  f->cache = NULL;
  f->lineinfo.init();
  f->upvalues.init();
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars.init();
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void luaF_freeproto (Proto *f) {
  f->code.clear();
  f->p.clear();
  f->constants.clear();
  f->lineinfo.clear();
  f->locvars.clear();
  f->upvalues.clear();
  luaM_delobject(f);
}


void luaF_freeclosure (Closure *c) {
  luaM_free(c->pupvals_);
  luaM_free(c->ppupvals_);
  luaM_delobject(c);
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

