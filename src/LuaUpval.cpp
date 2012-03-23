#include "LuaUpval.h"

UpVal::UpVal(LuaObject** gclist) : LuaObject(LUA_TUPVAL, gclist) {
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