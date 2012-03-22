#include "LuaProto.h"

Proto::Proto() : LuaObject(LUA_TPROTO, NULL) {
  cache = NULL;
  numparams = 0;
  is_vararg = 0;
  maxstacksize = 0;
  linedefined = 0;
  lastlinedefined = 0;
  source = NULL;
}