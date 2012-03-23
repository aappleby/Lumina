#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

#include "LuaGlobals.h"

void *luaM_alloc_ (size_t size, int type, int pool);

int LuaObject::instanceCounts[256];

LuaObject::LuaObject(int type) {

  next = NULL;
  if(thread_G) marked = luaC_white(thread_G);
  tt = type;

  LuaObject::instanceCounts[tt]++;
}

LuaObject::~LuaObject() {
  LuaObject::instanceCounts[tt]--;
}

void LuaObject::linkGC(LuaObject*& gcHead) {
  assert(next == NULL);
  next = gcHead;
  gcHead = this;
}

bool LuaObject::isDead() {
  uint8_t live = (marked ^ WHITEBITS) & (thread_G->currentwhite ^ WHITEBITS);
  return !live;
}