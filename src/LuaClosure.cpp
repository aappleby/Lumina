#include "LuaClosure.h"

#include "LuaCollector.h"
#include "LuaProto.h"
#include "LuaUpval.h"

// Lua closure
LuaClosure::LuaClosure(LuaProto* proto, int n) 
: LuaObject(LUA_TLCL) {
  linkGC(getGlobalGCList());

  isC = 0;
  nupvalues = n;
  pupvals_ = NULL;
  ppupvals_ = (LuaUpvalue**)luaM_alloc_nocheck(n * sizeof(LuaValue*));
  cfunction_ = NULL;
  proto_ = proto;

  while (n--) ppupvals_[n] = NULL;
}

// C closure
LuaClosure::LuaClosure(LuaCallback func, int n) 
: LuaObject(LUA_TCCL) {
  linkGC(getGlobalGCList());

  isC = 1;
  nupvalues = n;
  pupvals_ = (LuaValue*)luaM_alloc_nocheck(n * sizeof(LuaValue));
  ppupvals_ = NULL;
  cfunction_ = func;
  proto_ = NULL;
}

LuaClosure::LuaClosure(LuaCallback func, LuaValue upval1)
: LuaObject(LUA_TCCL) {
  linkGC(getGlobalGCList());

  isC = 1;
  nupvalues = 1;
  pupvals_ = (LuaValue*)luaM_alloc_nocheck(sizeof(LuaValue));
  ppupvals_ = NULL;
  cfunction_ = func;
  proto_ = NULL;

  pupvals_[0] = upval1;
}

LuaClosure::~LuaClosure() {
  luaM_free(pupvals_);
  luaM_free(ppupvals_);
  pupvals_ = NULL;
  ppupvals_ = NULL;
}

//------------------------------------------------------------------------------

void LuaClosure::VisitGC(LuaGCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);
}

int LuaClosure::PropagateGC(LuaGCVisitor& visitor) {
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

//------------------------------------------------------------------------------
