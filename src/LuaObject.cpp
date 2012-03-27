#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

#include "LuaGlobals.h"

void *luaM_alloc_ (size_t size, int type, int pool);

int LuaObject::instanceCounts[256];

LuaObject::LuaObject(int type) {

  next = NULL;
  marked = 0;
  if(thread_G) marked = thread_G->currentwhite & WHITEBITS;
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

bool LuaObject::isBlack() {
  return marked & (1 << BLACKBIT) ? true : false;
}

bool LuaObject::isWhite() {
  return marked & ((1 << WHITE0BIT) | (1 << WHITE1BIT)) ? true : false;
}

bool LuaObject::isGray() {
  return !isBlack() && !isWhite();
}

void LuaObject::changeWhite() {
  marked ^= WHITEBITS;
}

void LuaObject::grayToBlack() {
  marked |= (1 << BLACKBIT);
}

void LuaObject::resetOldBit() {
  marked &= ~(1 << OLDBIT);
}