#include "LuaString.h"

#include "LuaGlobals.h"

TString::TString() : LuaObject(LUA_TSTRING) {
  buf_ = NULL;
  reserved_ = 0;
  hash_ = 0;
  len_ = 0;
}

TString::~TString() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;

  thread_G->strings_->nuse_--;
}

//-----------------------------------------------------------------------------
// Stringtable

uint32_t hashString(const char* str, size_t len) {
  uint32_t hash = (uint32_t)len;

  size_t step = (len>>5)+1;  // if string is too long, don't hash all its chars
  size_t l1;
  for (l1 = len; l1 >= step; l1 -= step) {
    hash = hash ^ ((hash<<5)+(hash>>2) + (unsigned char)str[l1-1]);
  }

  return hash;
}

TString* stringtable::find(uint32_t hash, const char *str, size_t len) {

  LuaObject* o = hash_[hash & (size_-1)];

  for (; o != NULL; o = o->next) {
    TString *ts = dynamic_cast<TString*>(o);
    if(ts->getHash() != hash) continue;
    if(ts->getLen() != len) continue;

    if (memcmp(str, ts->c_str(), len * sizeof(char)) == 0) {
      // Found a match.
      return ts;
    }
  }
  return NULL;
}
