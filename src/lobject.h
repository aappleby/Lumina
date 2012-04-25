/*
** $Id: lobject.h,v 2.64 2011/10/31 17:48:22 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"

#include "LuaObject.h"
#include "LuaVector.h"
#include "LuaValue.h"
#include "LuaString.h"
#include "LuaTable.h"


int luaO_int2fb (unsigned int x);
int luaO_fb2int (int x);
int luaO_ceillog2 (unsigned int x);
double luaO_arith (int op, double v1, double v2);
int luaO_str2d (const char *s, size_t len, double *result);
int luaO_hexavalue (int c);
const char *luaO_pushvfstring (const char *fmt, va_list argp);
const char *luaO_pushfstring (LuaThread *L, const char *fmt, ...);
void luaO_chunkid (char *out, const char *source, size_t len);


#endif

