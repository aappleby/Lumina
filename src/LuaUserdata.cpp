#include "LuaUserdata.h"

#include "LuaTable.h"

#include "lmem.h"

Udata::Udata(size_t len) : LuaObject(LUA_TUSERDATA) {
  assert(l_memcontrol.limitDisabled);
  linkGC(getGlobalGCHead());
  buf_ = (uint8_t*)luaM_alloc_nocheck(len);
  len_ = len;
  metatable_ = NULL;
  env_ = NULL;
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