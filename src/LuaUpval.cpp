#include "LuaUpval.h"

#include "lmem.h"

UpVal::UpVal() : LuaObject(LUA_TUPVAL) {
  assert(l_memcontrol.limitDisabled);
  v = NULL;
  uprev = NULL;
  unext = NULL;
}

UpVal::~UpVal() {
  unlink();
}

void UpVal::unlink() {
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

void UpVal::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.MarkValue(*v);
  
  // closed? (open upvalues remain gray)
  if (v == &value) {
    setColor(BLACK);
  }
}

int UpVal::PropagateGC(GCVisitor& visitor) {
  assert(false);
  return 0;
}