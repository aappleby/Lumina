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

// Sanity check object state
void LuaObject::sanityCheck() {
  assert((marked & WHITEBITS) != WHITEBITS);
}

uint8_t LuaObject::getFlags() {
  return marked;
}

bool LuaObject::isDead() {
  uint8_t live = (marked ^ WHITEBITS) & (thread_G->currentwhite ^ WHITEBITS);
  return !live;
}

bool LuaObject::isWhite() {
  return marked & ((1 << WHITE0BIT) | (1 << WHITE1BIT)) ? true : false;
}

bool LuaObject::isGray() {
  return !isBlack() && !isWhite();
}

// Clear existing color + old bits, set color to current white.
void LuaObject::setWhite() {
  uint8_t mask = (1 << OLDBIT) | (1 << BLACKBIT) | (1 << WHITE0BIT) | (1 << WHITE1BIT);
  marked &= ~mask;
  marked |= (thread_G->currentwhite & WHITEBITS);
}

void LuaObject::changeWhite() {
  marked ^= WHITEBITS;
}

void LuaObject::whiteToGray() {
  marked &= ~WHITEBITS;
}

void LuaObject::blackToGray() {
  clearBlack();
}

void LuaObject::stringmark() {
  marked &= ~WHITEBITS;
}

void LuaObject::grayToBlack() {
  marked |= (1 << BLACKBIT);
}

bool LuaObject::isBlack()        { return marked & (1 << BLACKBIT) ? true : false; }
void LuaObject::setBlack()       { marked |= (1 << BLACKBIT); }
void LuaObject::clearBlack()     { marked &= ~(1 << BLACKBIT); }

bool LuaObject::isFinalized()    { return marked & (1 << FINALIZEDBIT) ? true : false; }
void LuaObject::setFinalized()   { marked |= (1 << FINALIZEDBIT); }
void LuaObject::clearFinalized() { marked &= ~(1 << FINALIZEDBIT); }

// TODO(aappleby): change to SEPARATEDBIT
bool LuaObject::isSeparated()    { return marked & (1 << SEPARATED) ? true : false; }
void LuaObject::setSeparated()   { marked |= (1 << SEPARATED); }
void LuaObject::clearSeparated() { marked &= ~(1 << SEPARATED); }

bool LuaObject::isFixed()        { return marked & (1 << FIXEDBIT) ? true : false; }
void LuaObject::setFixed()       { marked |= (1 << FIXEDBIT); }
void LuaObject::clearFixed()     { marked &= ~(1 << FIXEDBIT); }

/* MOVE OLD rule: whenever an object is moved to the beginning of
   a GC list, its old bit must be cleared */
bool LuaObject::isOld()          { return marked & (1 << OLDBIT) ? true : false; }
void LuaObject::setOld()         { marked |= (1 << OLDBIT); }
void LuaObject::clearOld()       { marked &= ~(1 << OLDBIT); }

bool LuaObject::isTestGray()     { return marked & (1 << TESTGRAYBIT) ? true : false; }
void LuaObject::setTestGray()    { marked |= (1 << TESTGRAYBIT); }
void LuaObject::clearTestGray()  { marked &= ~(1 << TESTGRAYBIT); }

extern char** luaT_typenames;
const char * LuaObject::typeName() const {
  return luaT_typenames[tt+1];
}