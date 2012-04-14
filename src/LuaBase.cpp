#include "LuaBase.h"
#include "lmem.h"

#include "LuaGlobals.h"

#include <assert.h>

void * LuaBase::operator new(size_t size) {
  assert(l_memcontrol.limitDisabled);
  void* blob = luaM_alloc_nocheck(size);
  if(blob && thread_G) {
    thread_G->incGCDebt(size);
  }
  return blob;
}

void LuaBase::operator delete(void* blob) {
  luaM_free(blob);
}

