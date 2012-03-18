#pragma once
#include "LuaTypes.h"

class LuaObject {
public:

  void Init(int type);

  bool isDead();
  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  LuaObject *next;
  uint8_t tt;
  uint8_t marked;
};

