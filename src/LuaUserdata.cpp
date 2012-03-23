#include "LuaUserdata.h"


Udata::Udata(uint8_t* buf, size_t len, Table* env) : LuaObject(LUA_TUSERDATA) {
  buf_ = buf;
  len_ = len;
  metatable_ = NULL;
  env_ = env;

  linkGC(getGlobalGCHead());
}

Udata::~Udata() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;
}