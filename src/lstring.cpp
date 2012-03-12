/*
** $Id: lstring.c,v 2.19 2011/05/03 16:01:57 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"



void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  /* cannot resize while GC is traversing strings */
  luaC_runtilstate(L, ~bitmask(GCSsweepstring));
  if (newsize > tb->size) {
    tb->hash = (LuaBase**)luaM_reallocv(L, tb->hash, tb->size, newsize, sizeof(LuaBase*));
    for (i = tb->size; i < newsize; i++) tb->hash[i] = NULL;
  }
  /* rehash */
  for (i=0; i<tb->size; i++) {
    LuaBase *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      LuaBase *next = gch(p)->next;  /* save next */
      unsigned int h = lmod(gco2ts(p)->getHash(), newsize);  /* new position */
      gch(p)->next = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      resetoldbit(p);  /* see MOVE OLD rule */
      p = next;
    }
  }
  if (newsize < tb->size) {
    /* shrinking slice must be empty */
    assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    tb->hash = (LuaBase**)luaM_reallocv(L, tb->hash, tb->size, newsize, sizeof(LuaBase*));
  }
  tb->size = newsize;
}


static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  size_t totalsize;  /* total size of TString object */
  LuaBase **list;  /* (pointer to) list where it will be inserted */
  TString *ts;
  stringtable *tb = &G(L)->strt;
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  if (tb->nuse >= cast(uint32_t, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  totalsize = sizeof(TString) + ((l + 1) * sizeof(char));
  list = &tb->hash[lmod(h, tb->size)];
  LuaBase* o = luaC_newobj(L, LUA_TSTRING, totalsize, list);
  ts = gco2ts(o);
  ts->setLen(l);
  ts->setHash(h);
  ts->setReserved(0);
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  tb->nuse++;
  return ts;
}


TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  LuaBase *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = gch(o)->next) {
    TString *ts = gco2ts(o);
    if (h == ts->getHash() &&
        ts->getLen() == l &&
        (memcmp(str, ts->c_str(), l * sizeof(char)) == 0)) {
      if (isdead(G(L), o))  /* string is dead (but was not collected yet)? */
        changewhite(o);  /* resurrect it */
      return ts;
    }
  }
  return newlstr(L, str, l, h);  /* not found; create a new string */
}


TString *luaS_new (lua_State *L, const char *str) {
  return luaS_newlstr(L, str, strlen(str));
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  LuaBase* o = luaC_newobj(L, LUA_TUSERDATA, sizeof(Udata) + s, NULL);
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  u->env = e;
  return u;
}

#define sizestring(s)	(sizeof(TString)+((s)->getLen()+1)*sizeof(char))

void luaS_freestr (lua_State* L, TString* ts) {
  luaM_freemem(L, ts, sizestring(ts));
}

void luaS_initstrt(stringtable * strt) {
  strt->size = 0;
  strt->nuse = 0;
  strt->hash = NULL;
}

void luaS_freestrt (lua_State* L, stringtable* strt) {
  luaM_freearray(L, strt->hash, strt->size);
}
