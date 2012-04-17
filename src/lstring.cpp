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
    ScopedMemChecker c;
    luaS_resize(tb->size_ * 2);  /* too crowded */
  }
  
  TString* ts = new TString(h, str, (int)l);

  LuaObject** list = &tb->hash_[h & (tb->size_ - 1)];
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
