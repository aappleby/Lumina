#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void LuaStackFrame::sanityCheck() {
  if(isLua()) {
    LuaProto *p = getFunc()->getLClosure()->proto_;
    assert(p->code.begin() <= savedpc);
    assert(savedpc <= p->code.end());
  }
}

int LuaStackFrame::getCurrentPC() {
  if(!isLua()) return -1;

  LuaProto* p = getFunc()->getLClosure()->proto_;
  return int((savedpc-1) - p->code.begin());
}

int LuaStackFrame::getCurrentLine() {
  if(!isLua()) return -1;

  LuaProto* p = getFunc()->getLClosure()->proto_;
  return p->getLine( getCurrentPC() );
}
