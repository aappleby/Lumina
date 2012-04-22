#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void CallInfo::sanityCheck() {
  if(isLua()) {
    Proto *p = getFunc()->getLClosure()->proto_;
    assert(p->code.begin() <= savedpc);
    assert(savedpc <= p->code.end());
  }
}