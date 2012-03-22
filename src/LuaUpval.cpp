#include "LuaUpval.h"

UpVal::UpVal(LuaObject** gclist) : LuaObject(LUA_TUPVAL, gclist) {
}