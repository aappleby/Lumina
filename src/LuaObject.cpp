#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

#include "LuaGlobals.h"

void *luaM_alloc_ (size_t size, int type, int pool);

int LuaObject::instanceCounts[256];

LuaObject::LuaObject(int type) {

  marked = luaC_white(thread_G);
  tt = type;

  LuaObject::instanceCounts[tt]++;
}

LuaObject::~LuaObject() {
  LuaObject::instanceCounts[tt]--;
}

void * LuaObject::operator new(size_t size) {
  void* blob = luaM_alloc(size);
  return blob;
}

void LuaObject::operator delete(void* blob) {
  luaM_free(blob);
}

void LuaObject::linkGC(LuaObject*& gcHead) {
  next = gcHead;
  gcHead = this;
}

bool LuaObject::isDead() {
  uint8_t live = (marked ^ WHITEBITS) & (thread_G->currentwhite ^ WHITEBITS);
  return !live;
}