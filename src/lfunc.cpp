/*
** $Id: lfunc.c,v 2.27 2010/06/30 14:11:17 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/


#include <stddef.h>

#define lfunc_c
#define LUA_CORE

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



Closure *luaF_newCclosure (lua_State *L, int n) {
  THREAD_CHECK(L);
  LuaBase* o = luaC_newobj(L, LUA_TFUNCTION, sizeCclosure(n), NULL);
  Closure *c = gco2cl(o);
  c->isC = 1;
  c->nupvalues = cast_byte(n);
  return c;
}


Closure *luaF_newLclosure (lua_State *L, Proto *p) {
  THREAD_CHECK(L);
  int n = p->sizeupvalues;
  LuaBase* o = luaC_newobj(L, LUA_TFUNCTION, sizeLclosure(n), NULL);
  Closure *c = gco2cl(o);
  c->isC = 0;
  c->p = p;
  c->nupvalues = cast_byte(n);
  while (n--) c->upvals[n] = NULL;
  return c;
}


UpVal *luaF_newupval (lua_State *L) {
  THREAD_CHECK(L);
  LuaBase *o = luaC_newobj(L, LUA_TUPVAL, sizeof(UpVal), NULL);
  UpVal *uv = gco2uv(o);
  uv->v = &uv->u.value;
  setnilvalue(uv->v);
  return uv;
}


UpVal *luaF_findupval (lua_State *L, StkId level) {
  THREAD_CHECK(L);
  global_State *g = G(L);
  LuaBase **pp = reinterpret_cast<LuaBase**>(&L->openupval);
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
    LuaBase *o = obj2gco(p);
    assert(p->v != &p->u.value);
    if (p->v == level) {  /* found a corresponding upvalue? */
      if (isdead(g, o))  /* is it dead? */
        changewhite(o);  /* resurrect it */
      return p;
    }
    resetoldbit(o);  /* may create a newer upval after this one */
    pp = &p->next;
  }
  /* not found: create a new one */
  LuaBase* o = luaC_newobj(L, LUA_TUPVAL, sizeof(UpVal), pp);
  uv = gco2uv(o);
  uv->v = level;  /* current value lives in the stack */
  uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
  uv->u.l.next = g->uvhead.u.l.next;
  uv->u.l.next->u.l.prev = uv;
  g->uvhead.u.l.next = uv;
  assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  return uv;
}


static void unlinkupval (UpVal *uv) {
  assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  uv->u.l.next->u.l.prev = uv->u.l.prev;  /* remove from `uvhead' list */
  uv->u.l.prev->u.l.next = uv->u.l.next;
}


void luaF_freeupval (lua_State *L, UpVal *uv) {
  THREAD_CHECK(L);
  if (uv->v != &uv->u.value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  luaM_freemem(L, uv, sizeof(UpVal));  /* free upvalue */
}


void luaF_close (lua_State *L, StkId level) {
  THREAD_CHECK(L);
  UpVal *uv;
  global_State *g = G(L);
  while (L->openupval != NULL && (uv = gco2uv(L->openupval))->v >= level) {
    LuaBase *o = obj2gco(uv);
    assert(!isblack(o) && uv->v != &uv->u.value);
    L->openupval = reinterpret_cast<UpVal*>(uv->next);  /* remove from `open' list */
    if (isdead(g, o))
      luaF_freeupval(L, uv);  /* free upvalue */
    else {
      unlinkupval(uv);  /* remove upvalue from 'uvhead' list */
      setobj(L, &uv->u.value, uv->v);  /* move value to upvalue slot */
      uv->v = &uv->u.value;  /* now current value lives here */
      gch(o)->next = g->allgc;  /* link upvalue into 'allgc' list */
      g->allgc = o;
      luaC_checkupvalcolor(g, uv);
    }
  }
}


Proto *luaF_newproto (lua_State *L) {
  THREAD_CHECK(L);
  LuaBase* o = luaC_newobj(L, LUA_TPROTO, sizeof(Proto), NULL);
  Proto* f = gco2p(o);
  f->constants = NULL;
  f->nconstants = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->cache = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void luaF_freeproto (lua_State *L, Proto *f) {
  THREAD_CHECK(L);
  luaM_freemem(L, f->code, f->sizecode * sizeof(Instruction));
  luaM_freemem(L, f->p, f->sizep * sizeof(Proto*));
  luaM_freemem(L, f->constants, f->nconstants * sizeof(TValue));
  luaM_freemem(L, f->lineinfo, f->sizelineinfo * sizeof(int));
  luaM_freemem(L, f->locvars, f->sizelocvars * sizeof(LocVar));
  luaM_freemem(L, f->upvalues, f->sizeupvalues * sizeof(Upvaldesc));
  luaM_freemem(L, f, sizeof(Proto));
}


void luaF_freeclosure (lua_State *L, Closure *c) {
  THREAD_CHECK(L);
  int size = (c->isC) ? sizeCclosure(c->nupvalues) :
                        sizeLclosure(c->nupvalues);
  luaM_freemem(L, c, size);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns NULL if not found.
*/
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return f->locvars[i].varname->c_str();
    }
  }
  return NULL;  /* not found */
}

