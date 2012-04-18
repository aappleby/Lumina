#include "LuaState.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaUpval.h"
#include "LuaValue.h"

#include <algorithm>

l_noret luaG_runerror (const char *fmt, ...);

lua_State::lua_State() : LuaObject(LUA_TTHREAD) {
  assert(l_memcontrol.limitDisabled);
  status = 0;
  l_G = NULL;
  oldpc = NULL;
  nonyieldable_count_ = 0;
  nCcalls = 0;
  hookmask = 0;
  allowhook = 0;
  basehookcount = 0;
  hookcount = 0;
  hook = NULL;
  errorJmp = NULL;
  errfunc = 0;
}

lua_State::~lua_State() {
  if(!stack_.empty()) {
    stack_.closeUpvals(stack_.begin());
  }
  stack_.free();
}

//-----------------------------------------------------------------------------

void lua_State::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);
}

// TODO(aappleby): Always clearing the unused part of the stack
// is safe, never clearing it is _not_ - something in the code
// is failing to clear the stack when top is moved.
int lua_State::PropagateGC(GCVisitor& visitor) {
  if (stack_.empty()) {
    // why do threads go on the 'grayagain' list?
    visitor.PushGrayAgain(this);
    return 1;
  }

  TValue* v = stack_.begin();
  for (; v < stack_.top_; v++) {
    visitor.MarkValue(*v);
  }

  for (; v < stack_.end(); v++) {
    *v = TValue::nil;
  }

  // why do threads go on the 'grayagain' list?
  visitor.PushGrayAgain(this);
  return TRAVCOST + int(stack_.top_ - stack_.begin());
}

//-----------------------------------------------------------------------------
