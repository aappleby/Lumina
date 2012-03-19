#include "LuaState.h"

#include "LuaValue.h"

void luaD_reallocstack (lua_State *L, int newsize);

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
    luaD_reallocstack(this, (int)goodsize);  /* shrink it */
  }
}


