#include "LuaTable.h"

Table::Table() : LuaObject(LUA_TTABLE, NULL) {
  metatable = NULL;
  flags = 0xFF;
}