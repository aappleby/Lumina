/*
** $Id: lobject.h,v 2.64 2011/10/31 17:48:22 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>
#include <string>

#include "llimits.h"
#include "lua.h"

#include "LuaObject.h"
#include "LuaVector.h"
#include "LuaValue.h"
#include "LuaString.h"
#include "LuaTable.h"


double luaO_arith (int op, double v1, double v2);
const char *luaO_pushvfstring (const char *fmt, va_list argp);
const char *luaO_pushfstring (LuaThread *L, const char *fmt, ...);

void pushstr(LuaThread* L, const std::string& s);

#endif

