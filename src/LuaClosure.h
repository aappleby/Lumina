#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

class LuaClosure : public LuaObject {
public:

  LuaClosure(LuaProto* proto, int n);
  LuaClosure(LuaCallback func, int n);
  ~LuaClosure();

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  int isC;
  int nupvalues;

  LuaValue* pupvals_;
  LuaUpvalue** ppupvals_;

  LuaCallback cfunction_;

  LuaProto *proto_;
};


