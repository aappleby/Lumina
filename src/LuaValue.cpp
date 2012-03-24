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
}

void TValue::sanitycheck() {
  if(isCollectable()) {
    assert(basetype() == gc->tt);
    assert(!gc->isDead());
  }
}

void setobj(TValue* obj1, const TValue* obj2) {
  const TValue *io2=(obj2);
  TValue *io1=(obj1);
  //io1->bytes = 0;
	io1->bytes = io2->bytes;
  io1->tt_ = io2->tt_;
	checkliveness(io1); 
}

void setobj2(TValue* obj1, const TValue* obj2) {
  const TValue *io2=(obj2);
  TValue *io1=(obj1);
  io1->bytes = 0;
	io1->bytes = io2->bytes;
  io1->tt_ = io2->tt_;
	checkliveness(io1); 
}
