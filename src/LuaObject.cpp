#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

void LuaObject::Init(int type) {
  global_State *g = thread_G;

  marked = luaC_white(g);
  tt = type;

  next = g->allgc;
  g->allgc = this;
}

bool LuaObject::isDead() {
  uint8_t live = (marked ^ WHITEBITS) & (thread_G->currentwhite ^ WHITEBITS);
  return !live;
}