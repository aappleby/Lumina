#include "LuaBase.h"
#include "lmem.h"

#include "LuaGlobals.h"

void * LuaBase::operator new(size_t size) {
  void* blob = luaM_alloc(size);
  /*
  if(blob && thread_G) {
    thread_G->incGCDebt(size);
  }
  */
  return blob;
}

void LuaBase::operator delete(void* blob) {
  luaM_free(blob);
}

