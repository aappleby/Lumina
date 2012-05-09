#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void LuaStackFrame::sanityCheck() {
  if(isLua()) {
    LuaProto *p = getFunc()->getLClosure()->proto_;
    assert(savedpc >= -1);
    assert(savedpc < (int)p->instructions_.size());
  }
}

int LuaStackFrame::beginInstruction() {
  savedpc++;
  //line_ = getFunc()->getLClosure()->proto_->getLine( savedpc );
  return code_[savedpc];
}

void LuaStackFrame::undoInstruction() {
  savedpc--;
}

int LuaStackFrame::getCurrentPC() {
  return savedpc;
}

int LuaStackFrame::getCurrentLine() {
  if(!isLua()) return -1;

  if(savedpc == -1) return -1;

  LuaProto *p = getFunc()->getLClosure()->proto_;
  return p->getLine( savedpc );
}

int LuaStackFrame::getCurrentInstruction() {
  if(!isLua()) return -1;

  return code_[savedpc];
}

int LuaStackFrame::getCurrentOp() {
  if(!isLua()) return -1;

  Instruction i = code_[savedpc];
  return (i & 0x0000003F);
}

int LuaStackFrame::getNextInstruction() {
  return code_[savedpc+1];
}

int LuaStackFrame::getNextOp() {
  if(!isLua()) return -1;

  Instruction i = code_[savedpc+1];
  return (i & 0x0000003F);
}

LuaValue* LuaStackFrame::getConstants() const {
 return constants_;
}


void LuaStackFrame::resetPC() {
  if(!isLua()) return;

  code_ = getFunc()->getLClosure()->proto_->instructions_.begin();
  constants_ = getFunc()->getLClosure()->proto_->constants.begin();
  savedpc = -1;
}