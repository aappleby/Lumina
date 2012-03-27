#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaObject : public LuaBase {
public:

  LuaObject(int type);
  ~LuaObject();

  void linkGC(LuaObject*& gcHead);

  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  bool isDead();
  bool isBlack();
  bool isWhite();
  bool isGray();

  void changeWhite();
  void grayToBlack();
  void resetOldBit();

  LuaObject *next;
  LuaObject *next_gray_;
  uint8_t tt;
  uint8_t marked;

  static int instanceCounts[256];
};
