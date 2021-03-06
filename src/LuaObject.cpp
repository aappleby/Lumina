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

//------------------------------------------------------------------------------

LuaObject::LuaObject(LuaType type) {
  prev_ = NULL;
  next_ = NULL;

  prev_gray_ = NULL;
  next_gray_ = NULL;

  flags_ = 0;
  color_ = thread_G ? thread_G->livecolor : GRAY;
  type_ = type;

  if(thread_G) thread_G->instanceCounts[type_]++;
}

LuaObject::~LuaObject() {
  assert(next_ == NULL);
  assert(prev_ == NULL);

  if(thread_G) thread_G->instanceCounts[type_]--;
}

//------------------------------------------------------------------------------

void LuaObject::linkGC(LuaList& gclist) {
  assert(prev_ == NULL);
  assert(next_ == NULL);

  gclist.Push(this);
}

void LuaObject::linkGC(LuaList& list, LuaObject* prev, LuaObject* next) {
  assert(prev_ == NULL);
  assert(next_ == NULL);

  if(prev == NULL) {
    list.Push(this);
    return;
  }

  if(next == NULL) {
    list.PushTail(this);
    return;
  }

  prev_ = prev;
  next_ = next;

  if(prev_) prev_->next_ = this;
  if(next_) next_->prev_ = this;
}

void LuaObject::unlinkGC(LuaList& list) {
  list.Detach(this);
}

//------------------------------------------------------------------------------

bool LuaObject::isBlack() {
  return color_ == BLACK; 
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

bool LuaObject::isLiveColor() {
  return color_ == thread_G->livecolor; 
}

bool LuaObject::isDeadColor() {
  return color_ == thread_G->deadcolor;
}

bool LuaObject::isDead() {  
  if(isFixed()) return false;
  return color_ == thread_G->deadcolor;
}

//------------------------------------------------------------------------------
// Clear existing color + old bits, set color to current white.

void LuaObject::makeLive() {
  clearOld();
  color_ = thread_G->livecolor;
}

//------------------------------------------------------------------------------

void LuaObject::VisitGC(LuaGCVisitor&) {
  // Should never be visiting the base class
  assert(false);
}

int LuaObject::PropagateGC(LuaGCVisitor&) {
  // Should never be propagating GC through the base class.
  assert(false);
  return 0;
}

//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------

extern char** luaT_typenames;
const char * LuaObject::typeName() const {
  return luaT_typenames[type_+1];
}

//------------------------------------------------------------------------------
