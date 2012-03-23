#include "LuaClosure.h"

Closure::Closure() : LuaObject(LUA_TFUNCTION) {
  linkGC(getGlobalGCHead());
}

Closure::~Closure() {
  luaM_free(pupvals_);
  luaM_free(ppupvals_);
  pupvals_ = NULL;
  ppupvals_ = NULL;
}
