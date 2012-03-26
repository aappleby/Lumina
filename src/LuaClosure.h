#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:

  Closure();
  ~Closure();

  uint8_t isC;
  uint8_t nupvalues;
  LuaObject *next_gray_;

  TValue* pupvals_;
  UpVal** ppupvals_;

  lua_CFunction f;

  Proto *p;
};


