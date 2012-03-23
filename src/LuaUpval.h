#pragma once
#include "LuaObject.h"
#include "LuaValue.h"

/*
** Lua Upvalues
*/
class UpVal : public LuaObject {
public:

  UpVal();
  ~UpVal();

  void unlink();

  TValue *v;  /* points to stack or to its own value */

  TValue value;  // the value (when closed)
  UpVal *uprev;
  UpVal *unext;
};


#define gco2uv(o)	check_exp((o)->tt == LUA_TUPVAL, reinterpret_cast<UpVal*>(o))
