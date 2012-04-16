#include "LuaState.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaUpval.h"
#include "LuaValue.h"

#include <algorithm>

l_noret luaG_runerror (const char *fmt, ...);
void luaC_checkupvalcolor (global_State *g, UpVal *uv);

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
    closeUpvals(stack_.begin());
  }
  freestack();
}

void lua_State::initstack() {
  stack_.resize_nocheck(BASIC_STACK_SIZE);

  stack_.top_ = stack_.begin();

  /* initialize first ci */
  CallInfo* ci = &stack_.callinfo_head_;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = stack_.top_;
  stack_.top_++;
  ci->top = stack_.top_ + LUA_MINSTACK;
}


void lua_State::freestack() {
  if (stack_.empty()) {
    // Stack not completely built yet - we probably ran out of memory while trying to create a thread.
    return;  
  }
  
  // free the entire 'ci' list
  stack_.callinfo_ = &stack_.callinfo_head_;
  CallInfo *ci = stack_.callinfo_head_.next;
  while (ci != NULL) {
    CallInfo* next = ci->next;
    luaM_free(ci);
    ci = next;
  }
  stack_.callinfo_head_.next = NULL;

  stack_.clear();
}

// The amount of stack "in use" includes everything up to the current
// top of the stack _plus_ anything referenced by an active callinfo.
int lua_State::stackinuse() {
  CallInfo *temp_ci;
  StkId lim = stack_.top_;
  for (temp_ci = stack_.callinfo_; temp_ci != NULL; temp_ci = temp_ci->previous) {
    assert(temp_ci->top <= stack_.last());
    if (lim < temp_ci->top) {
      lim = temp_ci->top;
    }
  }
  return (int)(lim - stack_.begin()) + 1;  /* part of stack in use */
}

// Resizes the stack so that it can hold at least 'size' more elements.
void lua_State::growstack(int size) {
  // Asking for more stack when we're already over the limit is  an error.
  if ((int)stack_.size() > LUAI_MAXSTACK)  /* error after extra size? */
    luaD_throw(LUA_ERRERR);

  // Asking for more space than could possibly fit on the stack is an error.
  int inuse = (int)(stack_.top_ - stack_.begin());
  int needed = inuse + size + EXTRA_STACK;
  if (needed > LUAI_MAXSTACK) {  /* stack overflow? */
    reallocstack(ERRORSTACKSIZE);
    luaG_runerror("stack overflow");
  }

  // Our new stack size should be either twice the current size,
  // or enough to hold what's already on the stack plus the
  // additional space - whichever's greater. Not more than
  // LUAI_MAXSTACK though.

  int newsize = std::max(2 * (int)stack_.size(), needed);
  newsize = std::min(newsize, LUAI_MAXSTACK);
  reallocstack(newsize);
}

void lua_State::shrinkstack() {
  size_t inuse = stackinuse();
  size_t goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (inuse > LUAI_MAXSTACK || goodsize >= stack_.size()) {
  } else {
    reallocstack((int)goodsize);  /* shrink it */
  }
}

// Resizes the stack and fixes up all pointers into the stack so they refer
// to the correct locations.
void lua_State::reallocstack (int newsize) {
  assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);

  // Remember where the old stack was. This will be a dangling pointer
  // after the resize, but that's OK as we only use it to fix up the
  // existing pointers - it doesn't get dereferenced.
  TValue *oldstack = stack_.begin();

  // Resize the stack array. but do not check to see if we've exceeded
  // our memory limit.
  stack_.resize_nocheck(newsize);

  // Correct the stack top pointer.
  stack_.top_ = stack_.begin() + (stack_.top_ - oldstack);
  
  // Correct all stack references in open upvalues.
  for (LuaObject* up = stack_.open_upvals_; up != NULL; up = up->next_) {
    UpVal* uv = static_cast<UpVal*>(up);
    uv->v = (uv->v - oldstack) + stack_.begin();
  }
  
  // Correct all stack references in all active callinfos.
  for (CallInfo* ci = stack_.callinfo_; ci != NULL; ci = ci->previous) {
    ci->top = (ci->top - oldstack) + stack_.begin();
    ci->func = (ci->func - oldstack) + stack_.begin();
    if ((ci->callstatus & CIST_LUA)) {
      ci->base = (ci->base - oldstack) + stack_.begin();
    }
  }

  // Stack is valid again, _now_ kick off memory errors if we're over the
  // limit.
  l_memcontrol.checkLimit();
}

void lua_State::checkstack(int size) {
  if ((stack_.last() - stack_.top_) <= size) growstack(size);
}

void lua_State::closeUpvals(StkId level) {
  UpVal *uv;

  while (stack_.open_upvals_ != NULL) {
    uv = dynamic_cast<UpVal*>(stack_.open_upvals_);
    if(uv->v < level) break;

    assert(!uv->isBlack() && uv->v != &uv->value);
    stack_.open_upvals_ = uv->next_;  /* remove from `open' list */

    if (uv->isDead())
      delete uv;
    else {
      uv->unlink();  /* remove upvalue from 'uvhead' list */

      uv->value = *uv->v;  /* move value to upvalue slot */
      uv->v = &uv->value;  /* now current value lives here */
      
      uv->next_ = thread_G->allgc;  /* link upvalue into 'allgc' list */
      thread_G->allgc = uv;

      luaC_checkupvalcolor(thread_G, uv);
    }
  }
}

// Positive stack indices are indexed from the current call frame.
// The first item in a call frame is the closure for the current function.
// Negative stack indices are indexed from the stack top.
// Negative indices less than or equal to LUA_REGISTRYINDEX are special.

TValue lua_State::at(int idx) {
  if (idx > 0) {
    TValue *o = stack_.callinfo_->func + idx;
    if (o >= stack_.top_) {
      assert(false);
      return TValue::None();
    }
    else return *o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return stack_.top_[idx];
  }

  if (idx == LUA_REGISTRYINDEX) {
    return thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (stack_.callinfo_->func->isLightFunction()) {
    assert(false);
    return TValue::None();
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  Closure* func = stack_.callinfo_->func->getCClosure();
  if(idx < func->nupvalues) {
    return func->pupvals_[idx];
  }

  // Invalid stack index.
  assert(false);
  return TValue::None();
}


TValue lua_State::pop() {
  stack_.top_--;
  return *stack_.top_;
}

void lua_State::push(TValue v) {
  stack_.top_[0] = v;
  stack_.top_++;
  assert((stack_.top_ <= stack_.callinfo_->top) && "stack overflow");
}

void lua_State::push(const TValue* v) {
  stack_.top_[0] = *v;
  stack_.top_++;
  assert((stack_.top_ <= stack_.callinfo_->top) && "stack overflow");
}

void lua_State::remove(int index) {
  assert(index > LUA_REGISTRYINDEX);
  TValue* p = (index > 0) ? &stack_.callinfo_->func[index] : &stack_.top_[index];
  while (++p < stack_.top_) {
    p[-1] = p[0];
  }
  stack_.top_--;
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
