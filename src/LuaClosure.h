#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:

  Closure(int type);
  ~Closure();

  uint8_t isC;
  uint8_t nupvalues;

  TValue* pupvals_;
  UpVal** ppupvals_;

  lua_CFunction f;

  Proto *p;
};


