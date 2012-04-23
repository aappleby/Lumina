#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class Closure : public LuaObject {
public:

  Closure(Proto* proto, int n);
  Closure(lua_CFunction func, int n);
  ~Closure();

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

  int isC;
  int nupvalues;

  TValue* pupvals_;
  UpVal** ppupvals_;

  lua_CFunction cfunction_;

  Proto *proto_;
};


