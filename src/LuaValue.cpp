#include "LuaValue.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaObject.h"
#include "LuaString.h"

uint32_t hash64 (uint32_t a, uint32_t b);
int luaO_str2d (const char *s, size_t len, lua_Number *result);

TValue TValue::LightUserdata(const void * p) {
  TValue v;
  v.type_ = LUA_TLIGHTUSERDATA;
  v.bytes_ = 0;
  v.pointer_ = (void*)p;
  return v;
}

TValue TValue::LightFunction(lua_CFunction f) {
  TValue v;
  v.type_ = LUA_TLCF;
  v.bytes_ = 0;
  v.callback_ = f;
  return v;
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
    return TValue(thread_G->strings_->Create(s, l));
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

bool TValue::isLiveColor() const {
  if(!isCollectable()) return false;
  return object_->isLiveColor();
}

uint32_t TValue::hashValue() const {
  return hash64(lowbytes_, highbytes_);
}

extern char** luaT_typenames;
const char * TValue::typeName() const {
  return luaT_typenames[type_];
}