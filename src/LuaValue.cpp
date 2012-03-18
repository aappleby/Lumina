#include "LuaValue.h"

void TValue::operator = ( TValue const & v )
{
  bytes = v.bytes;
  tt_ = v.tt_;
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
