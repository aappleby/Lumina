#include "LuaStack.h"

#include "LuaUpval.h"

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
    // Stack not completely built yet - we probably ran out of memory while trying to create a thread.
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
