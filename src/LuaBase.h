#pragma once

class LuaBase {
public:
  LuaBase *next;
  uint8_t tt;
  uint8_t marked;
};

