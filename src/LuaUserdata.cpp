#include "LuaUserdata.h"

#include "LuaTable.h"

#include "lmem.h"

Udata::Udata(uint8_t* buf, size_t len, Table* env) : LuaObject(LUA_TUSERDATA) {
  assert(l_memcontrol.limitDisabled);
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

int Udata::PropagateGC(GCVisitor& visitor) {
  assert(false);
  return 0;
}