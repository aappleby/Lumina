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

ScopedCallDepth::ScopedCallDepth(LuaThread* state) : state_(state) {
  state_->l_G->call_depth_++;
}

ScopedCallDepth::~ScopedCallDepth() {
  state_->l_G->call_depth_--;
}

//-----------------------------------------------------------------------------

LuaThread::LuaThread(LuaVM* g) : LuaObject(LUA_TTHREAD) {
  l_G = g;
  linkGC(l_G->allgc);

  oldpc = NULL;
  hookmask = 0;
  basehookcount = 0;
  hookcount = 0;
  hook = NULL;
  errfunc = 0;
  allowhook = 1;
  nonyieldable_count_ = 1;
  status = LUA_OK;

  setColor(l_G->livecolor);

  stack_.init();  /* init stack */
}

LuaThread::LuaThread(LuaThread* parent_thread) : LuaObject(LUA_TTHREAD) {
  l_G = parent_thread->l_G;
  linkGC(l_G->allgc);

  oldpc = NULL;
  hookmask = 0;
  basehookcount = 0;
  hookcount = 0;
  hook = NULL;
  errfunc = 0;
  allowhook = 1;
  nonyieldable_count_ = 1;
  status = LUA_OK;

  setColor(l_G->livecolor);

  stack_.init();  /* init stack */

  hookmask = parent_thread->hookmask;
  basehookcount = parent_thread->basehookcount;
  hook = parent_thread->hook;
  hookcount = parent_thread->basehookcount;
}

LuaThread::~LuaThread() {

  stack_.closeUpvals(stack_.begin());
  assert(stack_.open_upvals_.isEmpty());

  if(l_G) {
    if(this == l_G->mainthread) {
      l_G->mainthread = NULL;
    }
  }

  if(thread_L == this) {
    thread_L = NULL;
  }

  stack_.free();
}

//-----------------------------------------------------------------------------

void LuaThread::VisitGC(LuaGCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);
}

// TODO(aappleby): Always clearing the unused part of the stack
// is safe, never clearing it is _not_ - something in the code
// is failing to clear the stack when top is moved.
int LuaThread::PropagateGC(LuaGCVisitor& visitor) {
  if (stack_.empty()) {
    // why do threads go on the 'grayagain' list?
    visitor.PushGrayAgain(this);
    return 1;
  }

  LuaValue* v = stack_.begin();
  for (; v < stack_.top_; v++) {
    visitor.MarkValue(*v);
  }

  for (; v < stack_.end(); v++) {
    *v = LuaValue::Nil();
  }

  // why do threads go on the 'grayagain' list?
  visitor.PushGrayAgain(this);
  return TRAVCOST + int(stack_.top_ - stack_.begin());
}

//-----------------------------------------------------------------------------

LuaExecutionState LuaThread::saveState(StkId top) {
  LuaExecutionState s;

  s.callinfo_ = stack_.callinfo_;
  s.allowhook = allowhook;
  s.nonyieldable_count_ = nonyieldable_count_;
  s.errfunc = errfunc;
  s.old_top = stack_.indexOf(top);

  return s;
}

void LuaThread::restoreState(LuaExecutionState s, int status, int nresults) {
  if(status != LUA_OK) {
    // Grab the error object off the stack
    LuaValue errobj;

    if(status == LUA_ERRMEM) {
      errobj = LuaValue(l_G->memerrmsg);
    }
    else if(status == LUA_ERRERR) {
      errobj = LuaValue(thread_G->strings_->Create("error in error handling"));
    }
    else {
      errobj = stack_.top_[-1];
    }

    // Restore the stack to where it was before the call
    StkId oldtop = stack_.atIndex(s.old_top);
    stack_.closeUpvals(oldtop);
    stack_.top_ = oldtop;

    // Put the error object on the restored stack
    stack_.push_nocheck(errobj);
    stack_.shrink();
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
