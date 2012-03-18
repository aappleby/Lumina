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
#include "ldebug.h"


void luaS_resize (lua_State *L, int newsize) {
  THREAD_CHECK(L);
  int i;
  stringtable *tb = G(L)->strt;
  /* cannot resize while GC is traversing strings */
  luaC_runtilstate(L, ~bitmask(GCSsweepstring));
  if (newsize > tb->size) {
    tb->hash.resize(newsize);
    for (i = tb->size; i < newsize; i++) tb->hash[i] = NULL;
  }
  /* rehash */
  for (i=0; i<tb->size; i++) {
    LuaObject *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      LuaObject *next = gch(p)->next;  /* save next */
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
    tb->hash.resize(newsize);
  }
  tb->size = newsize;
}


static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  THREAD_CHECK(L);
  size_t totalsize;  /* total size of TString object */
  LuaObject **list;  /* (pointer to) list where it will be inserted */
  TString *ts;
  stringtable *tb = G(L)->strt;
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaG_runerror("memory allocation error: string too big");
  if (tb->nuse >= cast(uint32_t, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  totalsize = sizeof(TString) + ((l + 1) * sizeof(char));
  list = &tb->hash[lmod(h, tb->size)];
  LuaObject* o = luaC_newobj(LUA_TSTRING, totalsize, list);
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
  THREAD_CHECK(L);
  LuaObject *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  for (o = G(L)->strt->hash[lmod(h, G(L)->strt->size)];
       o != NULL;
       o = gch(o)->next) {
    TString *ts = gco2ts(o);
    if (h == ts->getHash() &&
        ts->getLen() == l &&
        (memcmp(str, ts->c_str(), l * sizeof(char)) == 0)) {
      if (isdead(o))  /* string is dead (but was not collected yet)? */
        changewhite(o);  /* resurrect it */
      return ts;
    }
  }
  return newlstr(L, str, l, h);  /* not found; create a new string */
}


TString *luaS_new (lua_State *L, const char *str) {
  THREAD_CHECK(L);
  return luaS_newlstr(L, str, strlen(str));
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  THREAD_CHECK(L);
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaG_runerror("memory allocation error: udata too big");
  LuaObject* o = luaC_newobj(LUA_TUSERDATA, sizeof(Udata) + s, NULL);
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  u->env = e;
  return u;
}

#define sizestring(s)	(sizeof(TString)+((s)->getLen()+1)*sizeof(char))

void luaS_freestr (lua_State* L, TString* ts) {
  THREAD_CHECK(L);
  G(L)->strt->nuse--;
  luaM_delobject(ts, sizestring(ts), LUA_TSTRING);
}

void luaS_initstrt(stringtable * strt) {
  strt->size = 0;
  strt->nuse = 0;
  strt->hash.init();
}

void luaS_freestrt (lua_State* L, stringtable* strt) {
  THREAD_CHECK(L);
  strt->hash.clear();
}
