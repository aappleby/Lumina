#include "LuaState.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaString.h"
#include "LuaUpval.h"
#include "LuaValue.h"

#include <algorithm>

l_noret luaG_runerror (const char *fmt, ...);

#define restorestack(L,n)	((TValue *)((char *)L->stack_.begin() + (n)))

//-----------------------------------------------------------------------------

ScopedCallDepth::ScopedCallDepth(lua_State* state) : state_(state) {
  state_->l_G->call_depth_++;
}

ScopedCallDepth::~ScopedCallDepth() {
  state_->l_G->call_depth_--;
}

//-----------------------------------------------------------------------------

lua_State::lua_State() : LuaObject(LUA_TTHREAD) {
  assert(l_memcontrol.limitDisabled);
  status = 0;
  l_G = NULL;
  oldpc = NULL;
  nonyieldable_count_ = 0;
  hookmask = 0;
  allowhook = 0;
  basehookcount = 0;
  hookcount = 0;
  hook = NULL;
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

LuaExecutionState lua_State::saveState(ptrdiff_t old_top) {
  LuaExecutionState s;

  s.callinfo_ = stack_.callinfo_;
  s.allowhook = allowhook;
  s.nonyieldable_count_ = nonyieldable_count_;
  s.errfunc = errfunc;
  s.old_top = old_top;

  return s;
}

void lua_State::restoreState(LuaExecutionState s, int status) {
  if(status != LUA_OK) {
    // Error handling gets an exemption from the memory limit. Not doing so would mean that
    // reporting an out-of-memory error could itself cause another out-of-memory error, ad infinitum.
    l_memcontrol.disableLimit();

    // Grab the error object off the stack
    TValue errobj;

    if(status == LUA_ERRMEM) {
      errobj = TValue(l_G->memerrmsg);
    }
    else if(status == LUA_ERRERR) {
      errobj = TValue(TString::Create("error in error handling"));
    }
    else {
      errobj = stack_.top_[-1];
    }

    // Restore the stack to where it was before the call
    StkId oldtop = restorestack(this, s.old_top);
    stack_.closeUpvals(oldtop);
    stack_.top_ = oldtop;

    // Put the error object on the restored stack
    stack_.push_nocheck(errobj);
    
    stack_.shrink();

    l_memcontrol.enableLimit();
  }

  stack_.callinfo_ = s.callinfo_;
  allowhook = s.allowhook;
  nonyieldable_count_ = s.nonyieldable_count_;
  errfunc = s.errfunc;
}

//-----------------------------------------------------------------------------
