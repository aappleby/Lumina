#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaObject : public LuaBase {
public:

  LuaObject(int type);
  ~LuaObject();

  void linkGC(LuaObject*& gcHead);

  bool isDead();
  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  bool isBlack();
  bool isWhite();
  bool isGray();

  LuaObject *next;
  uint8_t tt;
  uint8_t marked;

  static int instanceCounts[256];
};
