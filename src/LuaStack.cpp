#include "LuaStack.h"

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaUpval.h"

#include <assert.h>

l_noret luaG_runerror (const char *fmt, ...);

//------------------------------------------------------------------------------
// Set up our starting stack & callinfo.

void LuaStack::init() {
  assert(empty());
  resize_nocheck(BASIC_STACK_SIZE);

  top_ = begin();

  /* initialize first ci */
  CallInfo* ci = &callinfo_head_;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = top_;
  top_++;
  ci->top =top_ + LUA_MINSTACK;
}

//------------------------------------------------------------------------------
// Resizes the stack and fixes up all pointers into the stack so they refer
// to the correct locations.

void LuaStack::realloc (int newsize) {
  assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);

  // Remember where the old stack was. This will be a dangling pointer
  // after the resize, but that's OK as we only use it to fix up the
  // existing pointers - it doesn't get dereferenced.
  TValue *oldstack = begin();

  // Resize the stack array. but do not check to see if we've exceeded
  // our memory limit.
  resize_nocheck(newsize);

  // Correct the stack top pointer.
  top_ = begin() + (top_ - oldstack);
  
  // Correct all stack references in open upvalues.
  for (LuaObject* up = open_upvals_; up != NULL; up = up->next_) {
    UpVal* uv = static_cast<UpVal*>(up);
    uv->v = (uv->v - oldstack) + begin();
  }
  
  // Correct all stack references in all active callinfos.
  for (CallInfo* ci = callinfo_; ci != NULL; ci = ci->previous) {
    ci->top = (ci->top - oldstack) + begin();
    ci->func = (ci->func - oldstack) + begin();
    if ((ci->callstatus & CIST_LUA)) {
      ci->base = (ci->base - oldstack) + begin();
    }
  }

  // Stack is valid again, _now_ kick off memory errors if we're over the
  // limit.
  l_memcontrol.checkLimit();
}

//------------------------------------------------------------------------------
// Unwind all callinfos and deallocate the stack.

void LuaStack::free() {
  if (empty()) {
    // Stack not completely built yet - we probably ran out of memory while
    // trying to create a thread.
    return;  
  }
  
  // free the entire 'ci' list
  callinfo_ = &callinfo_head_;
  CallInfo *ci = callinfo_head_.next;
  while (ci != NULL) {
    CallInfo* next = ci->next;
    luaM_free(ci);
    ci = next;
  }
  callinfo_head_.next = NULL;

  clear();
}

//------------------------------------------------------------------------------
// Resizes the stack so that it can hold at least 'size' more elements.

// Our new stack size should be either twice the current size,
// or enough to hold what's already on the stack plus the
// additional space - whichever's greater. Not more than
// LUAI_MAXSTACK though.

void LuaStack::grow(int extrasize) {
  // Asking for more stack when we're already over the limit is  an error.
  if ((int)size() > LUAI_MAXSTACK)  /* error after extra size? */
    luaD_throw(LUA_ERRERR);

  // Asking for more space than could possibly fit on the stack is an error.
  int inuse = (int)(top_ - begin());
  int needed = inuse + extrasize + EXTRA_STACK;
  if (needed > LUAI_MAXSTACK) {  /* stack overflow? */
    realloc(ERRORSTACKSIZE);
    luaG_runerror("stack overflow");
  }

  int newsize = std::max(2 * (int)size(), needed);
  newsize = std::min(newsize, LUAI_MAXSTACK);
  realloc(newsize);
}


//------------------------------------------------------------------------------
// Reduce the size of the stack to ~125% of what's necessary to store
// everything currently in use.

void LuaStack::shrink() {
  size_t inuse = countInUse();
  size_t goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (inuse > LUAI_MAXSTACK || goodsize >= size()) {
  } else {
    realloc((int)goodsize);  /* shrink it */
  }
}

//------------------------------------------------------------------------------

void LuaStack::reserve(int newsize) {
  if ((last() - top_) <= newsize) grow(newsize);
}

//------------------------------------------------------------------------------
// The amount of stack "in use" includes everything up to the current
// top of the stack _plus_ anything referenced by an active callinfo.

int LuaStack::countInUse() {
  CallInfo *temp_ci;
  StkId lim = top_;
  for (temp_ci = callinfo_; temp_ci != NULL; temp_ci = temp_ci->previous) {
    assert(temp_ci->top <= last());
    if (lim < temp_ci->top) {
      lim = temp_ci->top;
    }
  }
  return (int)(lim - begin()) + 1;  /* part of stack in use */
}

//------------------------------------------------------------------------------
// Positive stack indices are indexed from the current call frame.
// The first item in a call frame is the closure for the current function.
// Negative stack indices are indexed from the stack top.
// Negative indices less than or equal to LUA_REGISTRYINDEX are special.

TValue LuaStack::at(int idx) {
  if (idx > 0) {
    TValue *o = callinfo_->func + idx;
    if (o >= top_) {
      assert(false);
      return TValue::None();
    }
    else return *o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return top_[idx];
  }

  if (idx == LUA_REGISTRYINDEX) {
    return thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (callinfo_->func->isLightFunction()) {
    assert(false);
    return TValue::None();
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  Closure* func = callinfo_->func->getCClosure();
  if(idx < func->nupvalues) {
    return func->pupvals_[idx];
  }

  // Invalid stack index.
  assert(false);
  return TValue::None();
}

//------------------------------------------------------------------------------

TValue LuaStack::pop() {
  top_--;
  return *top_;
}

void LuaStack::push(TValue v) {
  top_[0] = v;
  top_++;
  assert((top_ <= callinfo_->top) && "stack overflow");
}

void LuaStack::push(const TValue* v) {
  top_[0] = *v;
  top_++;
  assert((top_ <= callinfo_->top) && "stack overflow");
}

void LuaStack::remove(int index) {
  assert(index > LUA_REGISTRYINDEX);
  TValue* p = (index > 0) ? &callinfo_->func[index] : &top_[index];
  while (++p < top_) {
    p[-1] = p[0];
  }
  top_--;
}

//------------------------------------------------------------------------------
