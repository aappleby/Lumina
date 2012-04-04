#include "LuaGlobals.h"
#include "LuaState.h"

LuaObject*& getGlobalGCHead() {
  return thread_G->allgc;
}

global_State::global_State() {
  GCdebt_ = 0;
  totalbytes_ = sizeof(lua_State) + sizeof(global_State);
}

global_State::~global_State() {
}