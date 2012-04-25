#include "LuaUpval.h"

#include "LuaCollector.h"

LuaUpvalue::LuaUpvalue(LuaObject** gchead) : LuaObject(LUA_TUPVALUE) {
  linkGC(gchead);
  v = &value;
  uprev = NULL;
  unext = NULL;
}

LuaUpvalue::~LuaUpvalue() {
  unlink();
}

void LuaUpvalue::unlink() {
  if(unext) {
    assert(unext->uprev == this);
    unext->uprev = uprev;
  }

  if(uprev) {
    assert(uprev->unext == this);
    uprev->unext = unext;
  }

  unext = NULL;
  uprev = NULL;
}

void LuaUpvalue::VisitGC(LuaGCVisitor& visitor) {
  setColor(GRAY);
  visitor.MarkValue(*v);
  
  // closed? (open upvalues remain gray)
  if (v == &value) {
    setColor(BLACK);
  }
}

int LuaUpvalue::PropagateGC(LuaGCVisitor&) {
  assert(false);
  return 0;
}