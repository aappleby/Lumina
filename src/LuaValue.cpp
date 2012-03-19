#include "LuaValue.h"

#include "LuaObject.h"

void TValue::operator = ( TValue const & v )
{
  bytes = v.bytes;
  tt_ = v.tt_;
  sanitycheck();
}

void TValue::operator = ( TValue * v )
{
  if(v) {
    bytes = v->bytes;
    tt_ = v->tt_;
  } else {
    bytes = 0;
    tt_ = LUA_TNIL;
  }
}

void TValue::sanitycheck() {
  if(isCollectable()) {
    assert(basetype() == gc->tt);
    assert(!gc->isDead());
  }
}
