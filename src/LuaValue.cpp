#include "LuaValue.h"

#include "LuaClosure.h"
#include "LuaObject.h"
#include "LuaString.h"

uint32_t hash64 (uint32_t a, uint32_t b);
int luaO_str2d (const char *s, size_t len, lua_Number *result);
TString *luaS_newlstr (const char *str, size_t l);

TValue TValue::nil;
TValue TValue::none(LUA_TNONE,0);

TValue TValue::None() {
  return TValue(LUA_TNONE,0);
}

TValue TValue::Nil() {
  return TValue(LUA_TNIL,0);
}

TValue::TValue(LuaObject* o) {
  bytes_ = 0;
  type_ = o->type();
  object_ = o;
  sanityCheck();
}

TValue::TValue(TString* v) {
  bytes_ = 0;
  type_ = LUA_TSTRING;
  object_ = (LuaObject*)v;
  sanityCheck();
}

TValue TValue::LightUserdata(void * p) {
  TValue v;
  v.type_ = LUA_TLIGHTUSERDATA;
  v.bytes_ = 0;
  v.pointer_ = p;
  return v;
}

TValue TValue::LightFunction(lua_CFunction f) {
  TValue v;
  v.type_ = LUA_TLCF;
  v.bytes_ = 0;
  v.callback_ = f;
  return v;
}

TValue TValue::CClosure(Closure* c) {
  TValue v;
  v.type_ = LUA_TCCL;
  v.bytes_ = 0;
  v.object_ = c;
  return v;
}

TValue TValue::LClosure(Closure* c) {
  TValue v;
  v.type_ = LUA_TLCL;
  v.bytes_ = 0;
  v.object_ = c;
  return v;
}


void TValue::operator = ( TValue v )
{
  bytes_ = v.bytes_;
  type_ = v.type_;
  sanityCheck();
}

void TValue::operator = ( TValue * v )
{
  if(this == v) return;
  bytes_ = 0; 
  if(v) {
    bytes_ = v->bytes_;
    type_ = v->type_;
  } else {
    assert(false);
    bytes_ = 0;
    type_ = LUA_TNIL;
  }

  sanityCheck();
}

void TValue::operator = (LuaObject* o) {
  assert(o);
  bytes_ = 0;
  type_ = o->type();
  object_ = o;
  sanityCheck();
}

TValue TValue::convertToNumber() const {
  if(isNumber()) return *this;

  if(isString()) {
    TString* s = getString();

    double result;
    if(luaO_str2d(s->c_str(), s->getLen(), &result)) {
      return TValue(result);
    }
  }

  return None();
}

TValue TValue::convertToString() const {
  if(isString()) return *this;

  if (isNumber()) {
    lua_Number n = getNumber();
    char s[LUAI_MAXNUMBER2STR];
    int l = lua_number2str(s, n);
    return TValue(luaS_newlstr(s, l));
  }

  return None();
}


void TValue::sanityCheck() const {
  if(isCollectable()) {
    object_->sanityCheck();
    assert(type_ == object_->type());
    assert(!object_->isDead());
  }
}

void TValue::typeCheck() const {
  if(isCollectable()) {
    assert(type_ == object_->type());
  }
}

bool TValue::isWhite() const {
  if(!isCollectable()) return false;
  return object_->isWhite();
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

uint32_t TValue::hashValue() const {
  return hash64(lowbytes_, highbytes_);
}

extern char** luaT_typenames;
const char * TValue::typeName() const {
  return luaT_typenames[type_];
}