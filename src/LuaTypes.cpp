#include "LuaTypes.h"

#include "lstate.h"

#include <assert.h>

__declspec(thread) lua_State* thread_L = NULL;
__declspec(thread) global_State* thread_G = NULL;

stringtable* getGlobalStringtable() {
  return thread_G->strt;
}

LuaScope::LuaScope(lua_State* L) {
  assert((thread_G == NULL) || (thread_G == L->l_G));
  oldState = thread_L;
  thread_L = L;
}
LuaScope::~LuaScope() {
  thread_L = oldState;
}


LuaGlobalScope::LuaGlobalScope(lua_State* L) {
  //assert(thread_G != L->l_G);
  oldState = thread_L;
  thread_L = L;
  thread_G = thread_L->l_G;
}

LuaGlobalScope::~LuaGlobalScope() {
  thread_L = oldState;
  thread_G = thread_L ? thread_L->l_G : NULL;
}
