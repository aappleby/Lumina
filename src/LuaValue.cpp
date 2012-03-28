#include "LuaValue.h"

#include "LuaObject.h"

TValue TValue::nil;

void TValue::operator = ( TValue const & v )
{
  bytes = v.bytes;
  tt_ = v.tt_;
  sanityCheck();
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

  sanityCheck();
}

void TValue::sanityCheck() const {
  if(isCollectable()) {
    gc->sanityCheck();
    assert(basetype() == gc->tt);
    assert(!gc->isDead());
  }
}

void TValue::typeCheck() const {
  if(isCollectable()) {
    assert(basetype() == gc->tt);
  }
}

void setobj(TValue* obj1, const TValue* obj2) {
  if(obj1 == obj2) return;
	obj1->bytes = obj2->bytes;
  obj1->tt_ = obj2->tt_;
	obj1->sanityCheck(); 
}

bool TValue::isWhite() const {
  if(!isCollectable()) return false;
  return gc->isWhite();
}