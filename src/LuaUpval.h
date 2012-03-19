#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

/*
** Lua Upvalues
*/
class UpVal : public LuaObject {
public:
  TValue *v;  /* points to stack or to its own value */

  TValue value;  // the value (when closed)
  UpVal *uprev;
  UpVal *unext;
};

