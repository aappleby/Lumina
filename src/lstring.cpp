/*
** $Id: lstring.c,v 2.19 2011/05/03 16:01:57 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaUserdata.h"

#include <string.h>

#include "MurmurHash3.h"

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ldebug.h"

void luaS_resize(int newsize) {
  // cannot resize while GC is traversing strings
  luaC_runtilstate(~(1 << GCSsweepstring));

  thread_G->strings_->resize(newsize);
}


static TString *newlstr (const char *str, size_t l, unsigned int h) {

  stringtable *tb = thread_G->strings_;
  if (tb->nuse_ >= cast(uint32_t, tb->size_) && tb->size_ <= MAX_INT/2) {
    luaS_resize(tb->size_ * 2);  /* too crowded */
    if(!l_memcontrol.limitDisabled && l_memcontrol.isOverLimit()) {
      luaD_throw(LUA_ERRMEM);
    }
  }
  
  char* buf = (char*)luaM_alloc(l+1);
  if(buf == NULL) return NULL;

  TString* ts = new TString(buf, h, str, l);
  if(ts == NULL) {
    luaM_free(buf);
    return NULL;
  }

  LuaObject*& list = tb->hash_[h & (tb->size_ - 1)];
  ts->linkGC(list);
  tb->nuse_++;
  return ts;
}

uint32_t hashString(const char* str, size_t len);

TString *luaS_newlstr (const char *str, size_t l) {

  unsigned int h = hashString(str,l);

  TString* ts = thread_G->strings_->find(h, str, l);
  if(ts) {
    if(ts->isDead()) ts->makeLive();
    return ts;
  }

  TString* s = newlstr(str, l, h);  /* not found; create a new string */
  if(s == NULL) luaD_throw(LUA_ERRMEM);
  return s;
}


TString *luaS_new (const char *str) {
  return luaS_newlstr(str, strlen(str));
}

Udata *luaS_newudata (size_t s, Table *e) {
  if (s > MAX_SIZET - sizeof(Udata)) luaG_runerror("memory allocation error: udata too big");

  uint8_t* b = (uint8_t*)luaM_alloc(s);
  if(b == NULL) return NULL;

  Udata* u = new Udata(b,s,e);
  if(u == NULL) {
    luaM_free(b);
    return NULL;
  }

  u->linkGC(getGlobalGCHead());
  return u;
}
