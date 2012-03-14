#pragma once
#include "LuaTypes.h"

class LuaBase {
public:

  void Init(lua_State* L, int type);

  LuaBase *next;
  uint8_t tt;
  uint8_t marked;
};

