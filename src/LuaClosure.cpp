#include "LuaClosure.h"

#include "LuaProto.h"
#include "LuaUpval.h"

// Lua closure
Closure::Closure(Proto* proto, int n) 
: LuaObject(LUA_TLCL) {
  assert(l_memcontrol.limitDisabled);
  linkGC(getGlobalGCHead());

  isC = 0;
  nupvalues = n;
  pupvals_ = NULL;
  ppupvals_ = (UpVal**)luaM_alloc_nocheck(n * sizeof(TValue*));
  cfunction_ = NULL;
  proto_ = proto;

  while (n--) ppupvals_[n] = NULL;
}

// C closure
Closure::Closure(lua_CFunction func, int n) 
: LuaObject(LUA_TCCL) {
  assert(l_memcontrol.limitDisabled);
  linkGC(getGlobalGCHead());

  isC = 1;
  nupvalues = n;
  pupvals_ = (TValue*)luaM_alloc_nocheck(n * sizeof(TValue));
  ppupvals_ = NULL;
  cfunction_ = func;
  proto_ = NULL;
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
    assert(nupvalues == (int)proto_->upvalues.size());
    visitor.MarkObject(proto_);
    for (int i=0; i< nupvalues; i++) {
      visitor.MarkObject(ppupvals_[i]);
    }
  }

  return 5 + nupvalues;
}