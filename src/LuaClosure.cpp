#include "LuaClosure.h"

#include "LuaProto.h"
#include "LuaUpval.h"

Closure::Closure(TValue* buf, int n) : LuaObject(LUA_TCCL) {
  assert(l_memcontrol.limitDisabled);
  linkGC(getGlobalGCHead());
  isC = 1;
  nupvalues = n;
  pupvals_ = buf;
  ppupvals_ = NULL;
}

Closure::Closure(Proto* proto, UpVal** buf, int n) : LuaObject(LUA_TLCL) {
  assert(l_memcontrol.limitDisabled);
  linkGC(getGlobalGCHead());
  isC = 0;
  nupvalues = n;
  proto_ = proto;
  pupvals_ = NULL;
  ppupvals_ = buf;
  while (n--) ppupvals_[n] = NULL;
}

Closure::~Closure() {
  luaM_free(pupvals_);
  luaM_free(ppupvals_);
  pupvals_ = NULL;
  ppupvals_ = NULL;
}

void Closure::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);
}

int Closure::PropagateGC(GCVisitor& visitor) {
  setColor(BLACK);
  if (isC) {
    for (int i=0; i< nupvalues; i++) {
      visitor.MarkValue(pupvals_[i]);
    }
  }
  else {
    assert(nupvalues == proto_->upvalues.size());
    visitor.MarkObject(proto_);
    for (int i=0; i< nupvalues; i++) {
      visitor.MarkObject(ppupvals_[i]);
    }
  }

  return 5 + nupvalues;
}