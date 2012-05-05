#include "LuaCallinfo.h"

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"


void LuaStackFrame::sanityCheck() {
  if(isLua()) {
    LuaProto *p = getFunc()->getLClosure()->proto_;
    assert(savedpc1 >= p->code.begin());
    assert(savedpc1 <= p->code.end());
    assert(savedpc2 >= p->code.begin());
    assert(savedpc2 <= p->code.end());
  }
}

int LuaStackFrame::beginInstruction() {
  int result1 = savedpc1[0];
  //int result2 = savedpc2[0];

  savedpc1++;

  /*
  if(result1 != result2) {
    int b = 0;
    b++;
  }
  */

  return result1;
}

void LuaStackFrame::endInstruction() {
  savedpc2++;
}

void LuaStackFrame::undoInstruction() {
  savedpc1--;
  savedpc2--;
}

int LuaStackFrame::getCurrentPC() {
  if(!isLua()) return -1;

  LuaProto* p = getFunc()->getLClosure()->proto_;
  int result1 = int((savedpc1-1) - p->code.begin());
  //int result2 = int(savedpc2 - p->code.begin());

  /*
  if(result1 != result2) {
    int b = 0;
    b++;
  }
  */

  return result1;
}

int LuaStackFrame::getCurrentLine() {
  if(!isLua()) return -1;

  LuaProto* p = getFunc()->getLClosure()->proto_;
  return p->getLine( getCurrentPC() );
}

int LuaStackFrame::getCurrentInstruction() {
  if(!isLua()) return -1;

  return savedpc1[-1];
}

int LuaStackFrame::getCurrentOp() {
  if(!isLua()) return -1;

  Instruction i = savedpc1[-1];
  return (i & 0x0000003F);
}

int LuaStackFrame::getNextInstruction() {
  return savedpc1[0];
}

int LuaStackFrame::getNextOp() {
  if(!isLua()) return -1;

  Instruction i = savedpc1[0];
  return (i & 0x0000003F);
}

void LuaStackFrame::resetPC() {
  if(!isLua()) return;

  savedpc1 = getFunc()->getLClosure()->proto_->code.begin();
  savedpc2 = getFunc()->getLClosure()->proto_->code.begin();
}