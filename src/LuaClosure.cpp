#include "LuaClosure.h"

Closure::Closure(TValue* buf, int n) : LuaObject(LUA_TCCL) {
  linkGC(getGlobalGCHead());
  isC = 1;
  nupvalues = n;
  pupvals_ = buf;
  ppupvals_ = NULL;
}

Closure::Closure(Proto* proto, UpVal** buf, int n) : LuaObject(LUA_TLCL) {
  linkGC(getGlobalGCHead());
  isC = 0;
  nupvalues = n;
  p = proto;
  pupvals_ = NULL;
  ppupvals_ = buf;
  while (n--) ppupvals_[n] = NULL;
}

Closure::~Closure() {
  luaM_free(pupvals_);
  luaM_free(ppupvals_);
  pupvals_ = NULL;
  ppupvals_ = NULL;
}
