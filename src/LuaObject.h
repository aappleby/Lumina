#pragma once
#include "LuaTypes.h"

class LuaObject {
public:

  LuaObject(int type);

  ~LuaObject();

  void* operator new(size_t size);
  void operator delete(void*);

  void linkGC(LuaObject*& gcHead);

  bool isDead();
  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  LuaObject *next;
  uint8_t tt;
  uint8_t marked;

  static int instanceCounts[256];
};
