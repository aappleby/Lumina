#include "LuaTypes.h"

#include "LuaGlobals.h"
#include "LuaState.h"

#include <assert.h>

l_noret luaG_typeerror (const TValue *o, const char *op);
l_noret luaG_runerror (const char *fmt, ...);
l_noret luaD_throw (int errcode);

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
  return thread_G->strings_;
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
  thread_G = L ? L->l_G : NULL;
}

LuaGlobalScope::~LuaGlobalScope() {
  thread_L = oldState;
  thread_G = oldState ? oldState->l_G : NULL;
}


void handleResult(LuaResult err, const TValue* val)
{
  // Handling errors can throw exceptions and must be done
  // outside of memory allocation blocks.
  assert(!l_memcontrol.limitDisabled);

  switch(err) {
    case LUA_OK:
      return;

    case LUA_ERRMEM:
      luaD_throw(LUA_ERRMEM);
      return;

    case LUA_ERRERR:
      luaD_throw(LUA_ERRERR);
      return;

    case LUA_ERRSTACK:
      luaG_runerror("stack overflow");
      return;

    case LUA_BAD_TABLE:
      luaG_typeerror(val, "index");
      return;

    case LUA_BAD_INDEX_TM:
      luaG_typeerror(val, "invalid type in __index method");
      return;

    case LUA_META_LOOP:
      luaG_runerror("loop in gettable");
      return;

    default:
      assert(false);
      return;
  }
}
