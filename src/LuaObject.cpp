#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

#include "LuaGlobals.h"

#define FINALIZEDBIT	3  /* object has been separated for finalization */
#define SEPARATED	4  /* object is in 'finobj' list or in 'tobefnz' */
#define OLDBIT		6  /* object is old (only in generational mode) */
#define TESTGRAYBIT		7 // bit 7 is currently used by tests (luaL_checkmemory)
#define FIXEDBIT	5  /* object is fixed (should not be collected) */

const LuaObject::Color LuaObject::colorA = WHITE0;
const LuaObject::Color LuaObject::colorB = WHITE1;


void *luaM_alloc_ (size_t size, int type, int pool);

int LuaObject::instanceCounts[256];

LuaObject::LuaObject(LuaType type) {

  next = NULL;
  flags_ = 0;
  color_ = thread_G ? thread_G->livecolor : GRAY;
  type_ = type;

  LuaObject::instanceCounts[type_]++;
}

LuaObject::~LuaObject() {
  LuaObject::instanceCounts[type_]--;
}

void LuaObject::linkGC(LuaObject*& gcHead) {
  assert(next == NULL);
  next = gcHead;
  gcHead = this;
}

// Sanity check object state
void LuaObject::sanityCheck() {
  bool colorOK = false;
  if(color_ == WHITE0) colorOK = true;
  if(color_ == WHITE1) colorOK = true;
  if(color_ == GRAY) colorOK = true;
  if(color_ == BLACK) colorOK = true;
  assert(colorOK);
}

uint8_t LuaObject::getFlags() {
  return flags_;
}

bool LuaObject::isDead() {  
  if(isFixed()) return false;
  if(color_ == BLACK) return false;
  if(color_ == GRAY) return false;
  return color_ != thread_G->livecolor;
}

bool LuaObject::isWhite() {
  if(thread_G && (color_ == thread_G->livecolor)) {
    return true;
  }
  if(color_ == WHITE0) {
    return true;
  }
  if(color_ == WHITE1) {
    return true;
  }
  return false;
}

bool LuaObject::isGray() {
  return color_ == GRAY;
}

// Clear existing color + old bits, set color to current white.
void LuaObject::makeLive() {
  clearOld();
  color_ = thread_G->livecolor;
}

void LuaObject::changeWhite() {
  makeLive();
}

void LuaObject::whiteToGray() {
  color_ = GRAY;
}

void LuaObject::blackToGray() {
  color_ = GRAY;
}

void LuaObject::stringmark() {
  color_ = GRAY;
}

void LuaObject::grayToBlack() {
  color_ = BLACK;
}

bool LuaObject::isBlack() {
  return color_ == BLACK; 
}

bool LuaObject::isFinalized()    { return flags_ & (1 << FINALIZEDBIT) ? true : false; }
void LuaObject::setFinalized()   { flags_ |= (1 << FINALIZEDBIT); }
void LuaObject::clearFinalized() { flags_ &= ~(1 << FINALIZEDBIT); }

// TODO(aappleby): change to SEPARATEDBIT
bool LuaObject::isSeparated()    { return flags_ & (1 << SEPARATED) ? true : false; }
void LuaObject::setSeparated()   { flags_ |= (1 << SEPARATED); }
void LuaObject::clearSeparated() { flags_ &= ~(1 << SEPARATED); }

bool LuaObject::isFixed()        { return flags_ & (1 << FIXEDBIT) ? true : false; }
void LuaObject::setFixed()       { flags_ |= (1 << FIXEDBIT); }
void LuaObject::clearFixed()     { flags_ &= ~(1 << FIXEDBIT); }

/* MOVE OLD rule: whenever an object is moved to the beginning of
   a GC list, its old bit must be cleared */
bool LuaObject::isOld()          { return flags_ & (1 << OLDBIT) ? true : false; }
void LuaObject::setOld()         { flags_ |= (1 << OLDBIT); }
void LuaObject::clearOld()       { flags_ &= ~(1 << OLDBIT); }

bool LuaObject::isTestGray()     { return flags_ & (1 << TESTGRAYBIT) ? true : false; }
void LuaObject::setTestGray()    { flags_ |= (1 << TESTGRAYBIT); }
void LuaObject::clearTestGray()  { flags_ &= ~(1 << TESTGRAYBIT); }

extern char** luaT_typenames;
const char * LuaObject::typeName() const {
  return luaT_typenames[type_+1];
}