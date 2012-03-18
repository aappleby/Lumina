#pragma once
#include "LuaTypes.h"
#include "LuaValue.h"

class LuaBase {
public:

  void Init(int type);

  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  LuaBase *next;
  uint8_t tt;
  uint8_t marked;
};

