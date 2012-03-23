#include "LuaTable.h"

Table::Table() : LuaObject(LUA_TTABLE, getGlobalGCHead()) {
  metatable = NULL;
  flags = 0xFF;
}