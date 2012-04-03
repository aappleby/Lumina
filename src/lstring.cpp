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

#define lmod(s,size) cast(int, (s) & ((size)-1))

void luaS_resize (int newsize) {
  int i;
  stringtable *tb = thread_G->strings_;
  /* cannot resize while GC is traversing strings */
  luaC_runtilstate(~(1 << GCSsweepstring));
  if (newsize > tb->size_) {
    tb->hash_.resize(newsize);
  }
  /* rehash */
  for (i=0; i<tb->size_; i++) {
    LuaObject *p = tb->hash_[i];
    tb->hash_[i] = NULL;
    while (p) {  /* for each node in the list */
      LuaObject *next = p->next;  /* save next */
      unsigned int h = lmod(dynamic_cast<TString*>(p)->getHash(), newsize);  /* new position */
      p->next = tb->hash_[h];  /* chain it */
      tb->hash_[h] = p;
      p->clearOld();  /* see MOVE OLD rule */
      p = next;
    }
  }
  if (newsize < tb->size_) {
    /* shrinking slice must be empty */
    assert(tb->hash_[newsize] == NULL && tb->hash_[tb->size_ - 1] == NULL);
    tb->hash_.resize(newsize);
  }
  tb->size_ = newsize;
}


static TString *newlstr (const char *str, size_t l, unsigned int h) {

  stringtable *tb = thread_G->strings_;
  if (tb->nuse_ >= cast(uint32_t, tb->size_) && tb->size_ <= MAX_INT/2)
    luaS_resize(tb->size_ * 2);  /* too crowded */
  
  char* buf = (char*)luaM_alloc(l+1);
  if(buf == NULL) return NULL;

  LuaObject*& list = tb->hash_[lmod(h, tb->size_)];
  TString* ts = new TString();
  if(ts == NULL) {
    luaM_free(buf);
    return NULL;
  }

  ts->linkGC(list);
  ts->setHash(h);
  ts->setReserved(0);
  ts->setBuf(buf);
  ts->setText(str, l);

  tb->nuse_++;
  return ts;
}

uint32_t hashString(const char* str, size_t len) {
  uint32_t hash = (uint32_t)len;

  size_t step = (len>>5)+1;  // if string is too long, don't hash all its chars
  size_t l1;
  for (l1 = len; l1 >= step; l1 -= step) {
    hash = hash ^ ((hash<<5)+(hash>>2)+cast(unsigned char, str[l1-1]));
  }

  return hash;
}

TString *luaS_newlstr (const char *str, size_t l) {

  unsigned int h = hashString(str,l);

  stringtable* strings = thread_G->strings_;

  LuaObject* o = thread_G->strings_->hash_[lmod(h, strings->size_)];
  for (; o != NULL; o = o->next) {
    TString *ts = dynamic_cast<TString*>(o);
    if(ts->getHash() != h) continue;
    if(ts->getLen() != l) continue;
    if (memcmp(str, ts->c_str(), l * sizeof(char)) != 0) continue;

    // Found a matching TString. Resurrect it and return it.
    if (ts->isDead()) ts->changeWhite();
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

void luaS_initstrt() {
  global_State* g = thread_G;
  g->strings_ = new stringtable();
  g->strings_->size_ = 0;
  g->strings_->nuse_ = 0;
}

void luaS_freestrt () {
  global_State* g = thread_G;
  g->strings_->hash_.clear();
  delete g->strings_;
  g->strings_ = NULL;
}
