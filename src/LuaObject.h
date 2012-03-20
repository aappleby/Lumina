#pragma once
#include "LuaTypes.h"

class LuaObject {
public:

  //LuaObject(int type, LuaObject** list);

  void Init(int type, LuaObject** list);

  //void* operator new (size_t size, int type);
  //void* operator delete ( void* );

  bool isDead();
  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  LuaObject *next;
  uint8_t tt;
  uint8_t marked;
};

