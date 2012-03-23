#include "LuaUserdata.h"


Udata::Udata(uint8_t* buf, size_t len, Table* env) : LuaObject(LUA_TUSERDATA, getGlobalGCHead()) {
  buf_ = buf;
  len_ = len;
  metatable_ = NULL;
  env_ = env;
}

Udata::~Udata() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;
}