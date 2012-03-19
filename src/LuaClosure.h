#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:
  uint8_t isC;
  uint8_t nupvalues;
  LuaObject *gclist;

  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */

  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
};


