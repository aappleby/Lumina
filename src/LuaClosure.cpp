#include "LuaClosure.h"

Closure::Closure(int type) : LuaObject(type) {
}

Closure::~Closure() {
  luaM_free(pupvals_);
  luaM_free(ppupvals_);
  pupvals_ = NULL;
  ppupvals_ = NULL;
}
