#include "LuaBase.h"
#include "lmem.h"

#include "LuaGlobals.h"

#include <assert.h>

void * LuaBase::operator new(size_t size) {
  void* blob = luaM_alloc_nocheck(size);
  if(blob && thread_G) {
    thread_G->incGCDebt((int)size);
  }
  return blob;
}

void LuaBase::operator delete(void* blob) {
  luaM_free(blob);
}

