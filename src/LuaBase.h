#pragma once

union GCObject;

class LuaBase {
public:
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
};

