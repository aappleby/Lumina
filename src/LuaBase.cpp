#include "LuaBase.h"
#include "lmem.h"

void * LuaBase::operator new(size_t size) {
  void* blob = luaM_alloc(size);
  return blob;
}

void LuaBase::operator delete(void* blob) {
  luaM_free(blob);
}

