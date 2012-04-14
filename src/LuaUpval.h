#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

/*
** Lua Upvalues
*/
class UpVal : public LuaObject {
public:

  UpVal(LuaObject** gchead);
  ~UpVal();

  void unlink();

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

  TValue *v;  /* points to stack or to its own value */

  TValue value;  // the value (when closed)
  UpVal *uprev;
  UpVal *unext;
};
