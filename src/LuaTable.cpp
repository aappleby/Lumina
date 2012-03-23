#include "LuaTable.h"

Table::Table() : LuaObject(LUA_TTABLE) {
  metatable = NULL;
  flags = 0xFF;

  linkGC(getGlobalGCHead());
}