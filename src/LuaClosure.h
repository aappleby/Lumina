#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:
  uint8_t isC;
  uint8_t nupvalues;
  LuaObject *gclist;

  TValue* pupvals_;
  UpVal** ppupvals_;

  lua_CFunction f;

  struct Proto *p;
};


