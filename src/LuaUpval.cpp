#include "LuaUpval.h"

UpVal::UpVal() : LuaObject(LUA_TUPVAL) {
}

UpVal::~UpVal() {
  if (v != &value) {
    assert((unext->uprev == this) && (uprev->unext == this));
    unext->uprev = uprev;
    uprev->unext = unext;
  }
}

void UpVal::unlink() {
  assert((unext->uprev == this) && (uprev->unext == this));
  unext->uprev = uprev;
  uprev->unext = unext;
}

void UpVal::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.MarkValue(*v);
  
  // closed? (open upvalues remain gray)
  if (v == &value) {
    setColor(BLACK);
  }
}