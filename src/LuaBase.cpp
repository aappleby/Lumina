#include "LuaBase.h"

void* luaM_alloc(size_t size);
void luaM_free(void* blob);

void * LuaBase::operator new(size_t size) {
  void* blob = luaM_alloc(size);
  return blob;
}

void LuaBase::operator delete(void* blob) {
  luaM_free(blob);
}

