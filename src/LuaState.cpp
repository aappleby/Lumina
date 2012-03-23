#include "LuaState.h"

#include "LuaUpval.h"
#include "LuaValue.h"
#include <algorithm>

l_noret luaG_runerror (const char *fmt, ...);

lua_State::lua_State() : LuaObject(LUA_TTHREAD) {
  linkGC(getGlobalGCHead());
}

lua_State::~lua_State() {
}

void lua_State::freeCI() {
  CallInfo *ci = ci_;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    luaM_free(ci);
  }
}


void lua_State::initstack() {
  stack.resize(BASIC_STACK_SIZE);
  top = stack.begin();
  stack_last = stack.end() - EXTRA_STACK;

  /* initialize first ci */
  CallInfo* ci = &base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = top;
  top++;
  ci->top = top + LUA_MINSTACK;
  ci_ = ci;
}


void lua_State::freestack() {
  if (stack.empty()) {
    // Stack not completely built yet - we probably ran out of memory while trying to create a thread.
    return;  
  }
  ci_ = &base_ci;  /* free the entire 'ci' list */
  freeCI();
  stack.clear();
}

// The amount of stack "in use" includes everything up to the current
// top of the stack _plus_ anything referenced by an active callinfo.
int lua_State::stackinuse() {
  CallInfo *temp_ci;
  StkId lim = top;
  for (temp_ci = ci_; temp_ci != NULL; temp_ci = temp_ci->previous) {
    assert(temp_ci->top <= stack_last);
    if (lim < temp_ci->top) {
      lim = temp_ci->top;
    }
  }
  return (int)(lim - stack.begin()) + 1;  /* part of stack in use */
}

// Resizes the stack so that it can hold at least 'size' more elements.
void lua_State::growstack(int size) {
  // Asking for more stack when we're already over the limit is  an error.
  if ((int)stack.size() > LUAI_MAXSTACK)  /* error after extra size? */
    luaD_throw(LUA_ERRERR);

  // Asking for more space than could possibly fit on the stack is an error.
  int inuse = (int)(top - stack.begin());
  int needed = inuse + size + EXTRA_STACK;
  if (needed > LUAI_MAXSTACK) {  /* stack overflow? */
    reallocstack(ERRORSTACKSIZE);
    luaG_runerror("stack overflow");
  }

  // Our new stack size should be either twice the current size,
  // or enough to hold what's already on the stack plus the
  // additional space - whichever's greater. Not more than
  // LUAI_MAXSTACK though.

  int newsize = std::max(2 * (int)stack.size(), needed);
  newsize = std::min(newsize, LUAI_MAXSTACK);
  reallocstack(newsize);
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
