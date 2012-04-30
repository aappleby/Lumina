#include "LuaValue.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaObject.h"
#include "LuaString.h"

int luaO_str2d (const char *s, size_t len, double *result);

//-----------------------------------------------------------------------------

uint32_t hash64 (uint32_t a, uint32_t b) {
  a ^= a >> 16;
  a *= 0x85ebca6b;
  a ^= a >> 13;
  a *= 0xc2b2ae35;
  a ^= a >> 16;

  a ^= b;

  a ^= a >> 16;
  a *= 0x85ebca6b;
  a ^= a >> 13;
  a *= 0xc2b2ae35;
  a ^= a >> 16;

  return a;
}

//-----------------------------------------------------------------------------

LuaValue LuaValue::convertToNumber() const {
  if(isNumber()) return *this;

  if(isString()) {
    LuaString* s = getString();

    double result;
    if(luaO_str2d(s->c_str(), s->getLen(), &result)) {
      return LuaValue(result);
    }
  }

  return None();
}

//-----------------------------------------------------------------------------

LuaValue LuaValue::convertToString() const {
  if(isString()) return *this;

  if (isNumber()) {
    char s[32];
    sprintf(s, "%.14g", getNumber());
    return LuaValue(thread_G->strings_->Create(s));
  }

  return None();
}

//-----------------------------------------------------------------------------

void LuaValue::sanityCheck() const {
  if(isCollectable()) {
    object_->sanityCheck();
    assert(type_ == object_->type());
    assert(!object_->isDead());
  }
}

void LuaValue::typeCheck() const {
  if(isCollectable()) {
    assert(type_ == object_->type());
  }
}

extern char** luaT_typenames;
const char * LuaValue::typeName() const {
  return luaT_typenames[type_];
}

//-----------------------------------------------------------------------------

bool LuaValue::isWhite() const {
  if(!isCollectable()) return false;
  return object_->isWhite();
}

bool LuaValue::isLiveColor() const {
  if(!isCollectable()) return false;
  return object_->isLiveColor();
}

//-----------------------------------------------------------------------------

uint32_t LuaValue::hashValue() const {
  return hash64(halves_.lowbytes_, halves_.highbytes_);
}

//-----------------------------------------------------------------------------
