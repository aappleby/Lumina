#include "LuaBase.h"

#include "lgc.h"
#include "lstate.h"

void LuaBase::Init(int type) {
  global_State *g = thread_G;

  marked = luaC_white(g);
  tt = type;

  next = g->allgc;
  g->allgc = this;
}