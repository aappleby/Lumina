#include "LuaCallinfo.h"

#include "LuaState.h"


const StkId CallInfo::getFunc() const {
  StkId func = stack_->begin() + func_index_;
  return func; 
}

void  CallInfo::setFunc(StkId func) {
  assert(func >= stack_->begin());
  assert(func < stack_->end());

  func_index_ = func - stack_->begin();
}

const StkId CallInfo::getTop() const {
  StkId top = stack_->begin() + top_index_;
  return top; 
}

void  CallInfo::setTop(StkId top) {
  assert(top >= stack_->begin());
  assert(top < stack_->end());

  top_index_ = top - stack_->begin();
}

const StkId CallInfo::getBase() const {
  StkId base = stack_->begin() + base_index_;
  return base;
}

void  CallInfo::setBase(StkId base) {
  assert(base >= stack_->begin());
  assert(base < stack_->end());

  base_index_ = base - stack_->begin();
}
