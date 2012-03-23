// LuaBase exists solely to ensure that all objects (eventually) have a virtual destructor, and
// that they all get allocated through luaM_alloc/luaM_free.

#pragma once

class LuaBase
{
public:

  LuaBase() {}
  virtual ~LuaBase() {}

  void* operator new(size_t size);
  void operator delete(void*);
};