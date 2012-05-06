#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void LuaStackFrame::sanityCheck() {
  if(isLua()) {
    LuaProto *p = getFunc()->getLClosure()->proto_;
    assert(savedpc >= -1);
    assert(savedpc < (int)p->code.size());
  }
}

int LuaStackFrame::beginInstruction() {
  savedpc++;
  return code[savedpc];
}

void LuaStackFrame::undoInstruction() {
  savedpc--;
}

int LuaStackFrame::getCurrentPC() {
  return savedpc;
}

int LuaStackFrame::getCurrentLine() {
  if(!isLua()) return -1;

  LuaProto *p = getFunc()->getLClosure()->proto_;
  return p->getLine( savedpc );
}

int LuaStackFrame::getCurrentInstruction() {
  if(!isLua()) return -1;

  return code[savedpc];
}

int LuaStackFrame::getCurrentOp() {
  if(!isLua()) return -1;

  Instruction i = code[savedpc];
  return (i & 0x0000003F);
}

int LuaStackFrame::getNextInstruction() {
  return code[savedpc+1];
}

int LuaStackFrame::getNextOp() {
  if(!isLua()) return -1;

  Instruction i = code[savedpc+1];
  return (i & 0x0000003F);
}

void LuaStackFrame::resetPC() {
  if(!isLua()) return;

  code = getFunc()->getLClosure()->proto_->code.begin();
  savedpc = -1;
}