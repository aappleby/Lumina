#include "LuaTypes.h"

#include "LuaGlobals.h"
#include "LuaState.h"

#include <assert.h>

__declspec(thread) lua_State* thread_L = NULL;
__declspec(thread) global_State* thread_G = NULL;

char* luaT_typenames_[] = {
  "no value",
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
  "dummy",
  "proto",
  "upval",
  "deadkey",
  "<invalid>",
};

const char* ttypename(int tag) {
  return luaT_typenames_[tag + 1];
}

const char* objtypename(const TValue* v) {
  return luaT_typenames_[v->type() + 1];
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
