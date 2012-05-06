#include "LuaTypes.h"

#include "LuaGlobals.h"
#include "LuaState.h"

#include <assert.h>

l_noret luaG_typeerror (const LuaValue *o, const char *op);
LuaResult luaG_runerror (const char *fmt, ...);

__declspec(thread) LuaThread* thread_L = NULL;
__declspec(thread) LuaVM* thread_G = NULL;

char* luaT_typenames_[] = {
  "nil",
  "no value",
  "boolean",
  "number",
  "userdata",
  "function",

  "string",
  "table",
  "thread",
  "proto",
  "function",
  "function",
  "upval",
  "userdata",

  "<invalid>",
};

const char* ttypename(int tag) {
  return luaT_typenames_[tag];
}

const char* objtypename(const LuaValue* v) {
  return luaT_typenames_[v->type()];
}

char** luaT_typenames = &luaT_typenames_[0];

LuaStringTable* getGlobalStringtable() {
  return thread_G->strings_;
}

LuaScope::LuaScope(LuaThread* L) {
  assert((thread_G == NULL) || (thread_G == L->l_G));
  oldState = thread_L;
  thread_L = L;
}
LuaScope::~LuaScope() {
  thread_L = oldState;
}


LuaGlobalScope::LuaGlobalScope(LuaThread* L) {
  //assert(thread_G != L->l_G);
  oldState = thread_L;
  thread_L = L;
  thread_G = L ? L->l_G : NULL;
}

LuaGlobalScope::~LuaGlobalScope() {
  thread_L = oldState;
  thread_G = oldState ? oldState->l_G : NULL;
}

void throwError(LuaResult err) {
  throw err;
}

void handleResult(LuaResult err, const LuaValue* val)
{
  LuaResult result = LUA_OK;

  switch(err) {
    case LUA_OK:
      return;

    case LUA_ERRSTACK:
      result = luaG_runerror("stack overflow");
      handleResult(result);
      return;

    case LUA_BAD_TABLE:
      luaG_typeerror(val, "index");
      return;

    case LUA_BAD_INDEX_TM:
      luaG_typeerror(val, "invalid type in __index method");
      return;

    case LUA_META_LOOP:
      result = luaG_runerror("loop in gettable");
      handleResult(result);
      return;

    default:
      throwError(err);
      return;
  }
}
