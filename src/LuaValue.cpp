#include "LuaValue.h"

#include "LuaObject.h"

TValue TValue::nil;

TValue::TValue(LuaObject* o) {
  bytes = 0;
  tt_ = o->tt;
  gc = o;
  sanityCheck();
}

TValue::TValue(TString* v) {
  bytes = 0;
  tt_ = LUA_TSTRING;
  gc = (LuaObject*)v;
  sanityCheck();
}



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

void TValue::operator = (LuaObject* o) {
  assert(o);
  bytes = 0;
  tt_ = o->tt;
  gc = o;
  sanityCheck();
}

void TValue::sanityCheck() const {
  if(tt_ & 0x30) {
    assert((tt_ & 0xF) == LUA_TFUNCTION);
  }

  if(isCollectable()) {
    assert((gc->tt <= LUA_TUPVAL) || (((gc->tt & 0xF) == LUA_TFUNCTION) && (gc->tt < 0x3f)));
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

bool TValue::isCollectable() const {
  if((tt_ & 0x3F) == LUA_TSTRING) return true;
  if((tt_ & 0x3F) == LUA_TTABLE) return true;
  if((tt_ & 0x3F) == LUA_TUSERDATA) return true;
  if((tt_ & 0x3F) == LUA_TTHREAD) return true;
  if((tt_ & 0x3F) == LUA_TPROTO) return true;
  if((tt_ & 0x3F) == LUA_TUPVAL) return true;
  if((tt_ & 0x3F) == LUA_TLCL) return true;
  if((tt_ & 0x3F) == LUA_TCCL) return true;
  return false;
}

bool TValue::isFunction() const {
  if(tt_ == LUA_TLCL) return true;
  if(tt_ == LUA_TCCL) return true;
  if(tt_ == LUA_TLCF) return true;
  return false;
}