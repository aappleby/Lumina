#include "LuaUserdata.h"

#include "LuaCollector.h"
#include "LuaTable.h"

#include "lmem.h"

LuaBlob::LuaBlob(size_t len) : LuaObject(LUA_TBLOB) {
  linkGC(getGlobalGCList());
  buf_ = (uint8_t*)luaM_alloc_nocheck(len);
  len_ = len;
  metatable_ = NULL;
  env_ = NULL;
}

LuaBlob::~LuaBlob() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;
}

void LuaBlob::VisitGC(LuaGCVisitor& visitor) {
  setColor(LuaObject::GRAY);
  visitor.MarkObject(metatable_);
  visitor.MarkObject(env_);
  setColor(LuaObject::BLACK);
  return;
}

int LuaBlob::PropagateGC(LuaGCVisitor&) {
  assert(false);
  return 0;
}