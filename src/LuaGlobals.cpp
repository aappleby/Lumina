#include "LuaGlobals.h"

void * global_State::operator new(size_t size) {
  void* blob = luaM_alloc(size);
  return blob;
}

void global_State::operator delete(void* blob) {
  luaM_free(blob);
}

LuaObject*& getGlobalGCHead() {
  return thread_G->allgc;
}