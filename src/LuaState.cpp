#include "LuaState.h"

#include "LuaUpval.h"
#include "LuaValue.h"

int lua_State::stackinuse() {
  CallInfo *temp_ci;
  StkId lim = top;
  for (temp_ci = ci_; temp_ci != NULL; temp_ci = temp_ci->previous) {
    assert(temp_ci->top <= stack_last);
    if (lim < temp_ci->top) lim = temp_ci->top;
  }
  return (int)(lim - stack.begin()) + 1;  /* part of stack in use */
}


void lua_State::shrinkstack() {
  size_t inuse = stackinuse();
  size_t goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (inuse > LUAI_MAXSTACK || goodsize >= stack.size()) {
  } else {
    reallocstack((int)goodsize);  /* shrink it */
  }
}

// Resizes the stack and fixes up all pointers into the stack so they refer
// to the correct locations.
void lua_State::reallocstack (int newsize) {
  assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
  assert(stack_last == stack.end() - EXTRA_STACK);

  // Remember where the old stack was. This will be a dangling pointer
  // after the resize, but that's OK as we only use it to fix up the
  // existing pointers - it doesn't get dereferenced.
  TValue *oldstack = stack.begin();

  // Resize the stack array.
  stack.resize(newsize);
  stack_last = stack.end() - EXTRA_STACK;

  // Correct the stack top pointer.
  top = stack.begin() + (top - oldstack);
  
  // Correct all stack references in open upvalues.
  for (LuaObject* up = openupval; up != NULL; up = up->next) {
    UpVal* uv = static_cast<UpVal*>(up);
    uv->v = (uv->v - oldstack) + stack.begin();
  }
  
  // Correct all stack references in all active callinfos.
  for (CallInfo* ci = ci_; ci != NULL; ci = ci->previous) {
    ci->top = (ci->top - oldstack) + stack.begin();
    ci->func = (ci->func - oldstack) + stack.begin();
    if ((ci->callstatus & CIST_LUA)) {
      ci->base = (ci->base - oldstack) + stack.begin();
    }
  }
}
