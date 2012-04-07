#include "LuaUserdata.h"

#include "LuaTable.h"

Udata::Udata(uint8_t* buf, size_t len, Table* env) : LuaObject(LUA_TUSERDATA) {
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

void Udata::VisitGC(GCVisitor& visitor) {
  setColor(LuaObject::GRAY);
  visitor.MarkObject(metatable_);
  visitor.MarkObject(env_);
  setColor(LuaObject::BLACK);
  return;
}