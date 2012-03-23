#include "LuaUpval.h"

UpVal::UpVal(LuaObject*& gcHead) : LuaObject(LUA_TUPVAL, gcHead) {
}

UpVal::~UpVal() {
  if (v != &value) {
    assert((unext->uprev == this) && (uprev->unext == this));
    unext->uprev = uprev;
    uprev->unext = unext;
  }
}

void UpVal::unlink() {
  assert((unext->uprev == this) && (uprev->unext == this));
  unext->uprev = uprev;
  uprev->unext = unext;
}