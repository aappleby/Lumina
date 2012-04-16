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
#include <memory>

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

using std::auto_ptr;

UpVal *luaF_findupval (StkId level) {
  global_State *g = thread_G;
  LuaObject **pp = &thread_L->open_upvals_;
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL && (p = dynamic_cast<UpVal*>(*pp))->v >= level) {
    assert(p->v != &p->value);
    if (p->v == level) {  /* found a corresponding upvalue? */
      if (p->isDead())  /* is it dead? */
        p->makeLive();  /* resurrect it */
      return p;
    }
    p->clearOld();  /* may create a newer upval after this one */
    pp = &(p->next_);
  }
  /* not found: create a new one */
  uv = new UpVal(pp);
  uv->v = level;  /* current value lives in the stack */
  uv->uprev = &g->uvhead;  /* double link it in `uvhead' list */
  uv->unext = g->uvhead.unext;
  uv->unext->uprev = uv;
  g->uvhead.unext = uv;
  assert(uv->unext->uprev == uv && uv->uprev->unext == uv);
  return uv;
}

void luaF_close (StkId level) {
  UpVal *uv;
  lua_State* L = thread_L;

  while (L->open_upvals_ != NULL) {
    uv = dynamic_cast<UpVal*>(L->open_upvals_);
    if(uv->v < level) break;

    assert(!uv->isBlack() && uv->v != &uv->value);
    L->open_upvals_ = uv->next_;  /* remove from `open' list */

    if (uv->isDead())
      delete uv;
    else {
      uv->unlink();  /* remove upvalue from 'uvhead' list */

      uv->value = *uv->v;  /* move value to upvalue slot */
      uv->v = &uv->value;  /* now current value lives here */
      
      uv->next_ = thread_G->allgc;  /* link upvalue into 'allgc' list */
      thread_G->allgc = uv;

      luaC_checkupvalcolor(thread_G, uv);
    }
  }
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

