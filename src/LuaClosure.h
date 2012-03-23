#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:

  Closure() : LuaObject(LUA_TFUNCTION, getGlobalGCHead()) {
  }

  ~Closure() {
    luaM_free(pupvals_);
    luaM_free(ppupvals_);
    pupvals_ = NULL;
    ppupvals_ = NULL;
  }

  uint8_t isC;
  uint8_t nupvalues;
  LuaObject *gclist;

  TValue* pupvals_;
  UpVal** ppupvals_;

  lua_CFunction f;

  Proto *p;
};


