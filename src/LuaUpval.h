#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

/*
** Lua Upvalues
*/
class LuaUpvalue : public LuaObject {
public:

  LuaUpvalue();
  ~LuaUpvalue();

  void unlink();

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  LuaValue *v;  /* points to stack or to its own value */

  LuaValue value;  // the value (when closed)
  LuaUpvalue *uprev;
  LuaUpvalue *unext;
};
