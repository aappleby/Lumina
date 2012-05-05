#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void LuaStackFrame::sanityCheck() {
  if(isLua()) {
    LuaProto *p = getFunc()->getLClosure()->proto_;
    assert(savedpc >= p->code.begin());
    assert(savedpc <= p->code.end());
  }
}

int LuaStackFrame::beginInstruction() {
  int result = savedpc[0];
  savedpc++;
  return result;
}

void LuaStackFrame::endInstruction() {
}

void LuaStackFrame::undoInstruction() {
  savedpc--;
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

int LuaStackFrame::getCurrentInstruction() {
  if(!isLua()) return -1;

  return savedpc[-1];
}

int LuaStackFrame::getCurrentOp() {
  if(!isLua()) return -1;

  Instruction i = savedpc[-1];
  return (i & 0x0000003F);
}

int LuaStackFrame::getNextInstruction() {
  return savedpc[0];
}

int LuaStackFrame::getNextOp() {
  if(!isLua()) return -1;

  Instruction i = savedpc[0];
  return (i & 0x0000003F);
}

void LuaStackFrame::resetPC() {
  if(!isLua()) return;

  savedpc = getFunc()->getLClosure()->proto_->code.begin();
}