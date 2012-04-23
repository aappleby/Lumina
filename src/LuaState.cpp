#include "LuaState.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaString.h"
#include "LuaUpval.h"
#include "LuaValue.h"

#include <algorithm>

#include "ldo.h"

l_noret luaG_runerror (const char *fmt, ...);

//-----------------------------------------------------------------------------

ScopedCallDepth::ScopedCallDepth(lua_State* state) : state_(state) {
  state_->l_G->call_depth_++;
}

ScopedCallDepth::~ScopedCallDepth() {
  state_->l_G->call_depth_--;
}

//-----------------------------------------------------------------------------

lua_State::lua_State(global_State* g) : LuaObject(LUA_TTHREAD) {
  assert(l_memcontrol.limitDisabled);
  l_G = g;
  oldpc = NULL;
  hookmask = 0;
  basehookcount = 0;
  hookcount = 0;
  hook = NULL;
  errfunc = 0;
  allowhook = 1;
  nonyieldable_count_ = 1;
  status = LUA_OK;

  setColor(g->livecolor);

  stack_.init();  /* init stack */
}

lua_State::~lua_State() {

  stack_.closeUpvals(stack_.begin());
  assert(stack_.open_upvals_ == NULL);

  if(l_G) {
    if(this == l_G->mainthread) {
      l_G->mainthread = NULL;
    }
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
    *v = TValue::Nil();
  }

  // why do threads go on the 'grayagain' list?
  visitor.PushGrayAgain(this);
  return TRAVCOST + int(stack_.top_ - stack_.begin());
}

//-----------------------------------------------------------------------------

LuaExecutionState lua_State::saveState(StkId top) {
  LuaExecutionState s;

  s.callinfo_ = stack_.callinfo_;
  s.allowhook = allowhook;
  s.nonyieldable_count_ = nonyieldable_count_;
  s.errfunc = errfunc;
  s.old_top = savestack(this, top);

  return s;
}

void lua_State::restoreState(LuaExecutionState s, int status, int nresults) {
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
      errobj = TValue(thread_G->strings_->Create("error in error handling"));
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

  if ((nresults == LUA_MULTRET) && (stack_.callinfo_->getTop() < stack_.top_)) {
    stack_.callinfo_->setTop(stack_.top_);
  }
}

//-----------------------------------------------------------------------------
