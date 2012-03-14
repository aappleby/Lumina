#include "LuaBase.h"

#include "lgc.h"
#include "lstate.h"

void LuaBase::Init(lua_State* L, int type) {
  THREAD_CHECK(L);
  global_State *g = G(L);

  marked = luaC_white(g);
  tt = type;

  next = g->allgc;
  g->allgc = this;
}