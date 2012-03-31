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

  thread_G->strt->nuse--;
}
