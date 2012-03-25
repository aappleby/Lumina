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
  bytes = 0; 
  if(v) {
    bytes = v->bytes;
    tt_ = v->tt_;
  } else {
    bytes = 0;
    tt_ = LUA_TNIL;
  }

  sanitycheck();
}

void TValue::sanitycheck() {
  if(isCollectable()) {
    assert(basetype() == gc->tt);
    assert(!gc->isDead());
  }
}

void setobj(TValue* obj1, const TValue* obj2) {
  if(obj1 == obj2) return;
	obj1->bytes = obj2->bytes;
  obj1->tt_ = obj2->tt_;
	checkliveness(obj1); 
}
