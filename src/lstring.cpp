/*
** $Id: lstring.c,v 2.19 2011/05/03 16:01:57 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"

#include <string.h>

#include "MurmurHash3.h"

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ldebug.h"

#define lmod(s,size) cast(int, (s) & ((size)-1))

void luaS_resize (int newsize) {
  int i;
  stringtable *tb = thread_G->strt;
  /* cannot resize while GC is traversing strings */
  luaC_runtilstate(~bitmask(GCSsweepstring));
  if (newsize > tb->size) {
    tb->hash.resize(newsize);
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


static TString *newlstr (const char *str, size_t l, unsigned int h) {

  LuaObject **list;  /* (pointer to) list where it will be inserted */
  TString *ts;
  stringtable *tb = thread_G->strt;
  if (tb->nuse >= cast(uint32_t, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(tb->size*2);  /* too crowded */
  list = &tb->hash[lmod(h, tb->size)];

  char* buf = (char*)luaM_alloc(l+1);
  if(buf == NULL) luaD_throw(LUA_ERRMEM);

  LuaObject* o = luaC_newobj(LUA_TSTRING, sizeof(TString), list);
  if(o == NULL) {
    luaM_free(buf);
    luaD_throw(LUA_ERRMEM);
  }

  LuaObject::instanceCounts[LUA_TSTRING]++;
  ts = gco2ts(o);
  ts->setHash(h);
  ts->setReserved(0);
  ts->setBuf(buf);
  ts->setText(str, l);

  tb->nuse++;
  return ts;
}


TString *luaS_newlstr (const char *str, size_t l) {
  LuaObject *o;

  unsigned int h = cast(unsigned int, l);  // seed
  size_t step = (l>>5)+1;  // if string is too long, don't hash all its chars
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  // compute hash
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));

  stringtable* strings = thread_G->strt;

  for (o = strings->hash[lmod(h, strings->size)]; o != NULL; o = gch(o)->next) {
    TString *ts = gco2ts(o);
    if (h == ts->getHash() &&
        ts->getLen() == l &&
        (memcmp(str, ts->c_str(), l * sizeof(char)) == 0)) {
      if (isdead(o))  /* string is dead (but was not collected yet)? */
        changewhite(o);  /* resurrect it */
      return ts;
    }
  }
  return newlstr(str, l, h);  /* not found; create a new string */
}


TString *luaS_new (const char *str) {
  return luaS_newlstr(str, strlen(str));
}

Udata *luaS_newudata (size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata)) luaG_runerror("memory allocation error: udata too big");

  uint8_t* b = (uint8_t*)luaM_alloc(s);
  if(b == NULL) luaD_throw(LUA_ERRMEM);

  LuaObject* o = luaC_newobj(LUA_TUSERDATA, sizeof(Udata), NULL);
  if(o == NULL) {
    luaM_free(b);
    luaD_throw(LUA_ERRMEM);
  }

  LuaObject::instanceCounts[LUA_TUSERDATA]++;
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  u->env = e;
  u->buf = b;
  return u;
}

void luaS_deludata(Udata* ud) {
  luaM_free(ud->buf);
  luaM_delobject(ud);
  LuaObject::instanceCounts[LUA_TUSERDATA]--;
}

void luaS_freestr (TString* ts) {
  thread_G->strt->nuse--;
  luaM_free((void*)ts->c_str());
  luaM_delobject(ts);
  LuaObject::instanceCounts[LUA_TSTRING]--;
}

void luaS_initstrt() {
  global_State* g = thread_G;
  g->strt = new stringtable();
  g->strt->size = 0;
  g->strt->nuse = 0;
  g->strt->hash.init();
}

void luaS_freestrt () {
  global_State* g = thread_G;
  g->strt->hash.clear();
  delete g->strt;
  g->strt = NULL;
}
