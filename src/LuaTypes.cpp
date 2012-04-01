#include "LuaTypes.h"

#include "LuaGlobals.h"
#include "LuaState.h"

#include <assert.h>

__declspec(thread) lua_State* thread_L = NULL;
__declspec(thread) global_State* thread_G = NULL;

char* luaT_typenames_[] = {
  "nil",
  "boolean",
  "userdata",
  "number",
  "string",
  "table",
  "function",
  "userdata",
  "thread",
  "function",
  "function",
  "proto",
  "upval",
  "deadkey",
  "no value",
  "<invalid>",
};

const char* ttypename(int tag) {
  return luaT_typenames_[tag];
}

const char* objtypename(const TValue* v) {
  return luaT_typenames_[v->type()];
}

char** luaT_typenames = &luaT_typenames_[0];

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
  thread_G = (thread_L) ? thread_L->l_G : NULL;
}

LuaGlobalScope::~LuaGlobalScope() {
  thread_L = oldState;
  thread_G = thread_L ? thread_L->l_G : NULL;
}
