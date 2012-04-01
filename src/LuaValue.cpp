#include "LuaValue.h"

#include "LuaClosure.h"
#include "LuaObject.h"

TValue TValue::nil;
TValue TValue::none(LUA_TNONE,0);

TValue::TValue(LuaObject* o) {
  bytes = 0;
  type_ = o->tt;
  gc = o;
  sanityCheck();
}

TValue::TValue(TString* v) {
  bytes = 0;
  type_ = LUA_TSTRING;
  gc = (LuaObject*)v;
  sanityCheck();
}

TValue TValue::LightUserdata(void * p) {
  TValue v;
  v.type_ = LUA_TLIGHTUSERDATA;
  v.bytes = 0;
  v.p = p;
  return v;
}

TValue TValue::LightFunction(lua_CFunction f) {
  TValue v;
  v.type_ = LUA_TLCF;
  v.bytes = 0;
  v.f = f;
  return v;
}

TValue TValue::CClosure(Closure* c) {
  TValue v;
  v.type_ = LUA_TCCL;
  v.bytes = 0;
  v.gc = c;
  return v;
}

TValue TValue::LClosure(Closure* c) {
  TValue v;
  v.type_ = LUA_TLCL;
  v.bytes = 0;
  v.gc = c;
  return v;
}


void TValue::operator = ( TValue v )
{
  bytes = v.bytes;
  type_ = v.type_;
  sanityCheck();
}

void TValue::operator = ( TValue * v )
{
  if(this == v) return;
  bytes = 0; 
  if(v) {
    bytes = v->bytes;
    type_ = v->type_;
  } else {
    assert(false);
    bytes = 0;
    type_ = LUA_TNIL;
  }

  sanityCheck();
}

void TValue::operator = (LuaObject* o) {
  assert(o);
  bytes = 0;
  type_ = o->tt;
  gc = o;
  sanityCheck();
}

void TValue::sanityCheck() const {
  if(isCollectable()) {
    gc->sanityCheck();
    assert(type_ == gc->tt);
    assert(!gc->isDead());
  }
}

void TValue::typeCheck() const {
  if(isCollectable()) {
    assert(type_ == gc->tt);
  }
}

void setobj(TValue* obj1, const TValue* obj2) {
  if(obj1 == obj2) return;
  (*obj1) = (*obj2);
	obj1->sanityCheck(); 
}

bool TValue::isWhite() const {
  if(!isCollectable()) return false;
  return gc->isWhite();
}

bool TValue::isCollectable() const {
  if((type_ & 0x3F) == LUA_TSTRING) return true;
  if((type_ & 0x3F) == LUA_TTABLE) return true;
  if((type_ & 0x3F) == LUA_TUSERDATA) return true;
  if((type_ & 0x3F) == LUA_TTHREAD) return true;
  if((type_ & 0x3F) == LUA_TPROTO) return true;
  if((type_ & 0x3F) == LUA_TUPVAL) return true;
  if((type_ & 0x3F) == LUA_TLCL) return true;
  if((type_ & 0x3F) == LUA_TCCL) return true;
  return false;
}

bool TValue::isFunction() const {
  if(type_ == LUA_TLCL) return true;
  if(type_ == LUA_TCCL) return true;
  if(type_ == LUA_TLCF) return true;
  return false;
}