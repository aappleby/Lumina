/*
** $Id: lualib.h,v 1.43 2011/12/08 12:11:37 roberto Exp $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef lualib_h
#define lualib_h

#include "lua.h"



int (luaopen_base) (LuaThread *L);

#define LUA_COLIBNAME	"coroutine"
int (luaopen_coroutine) (LuaThread *L);

#define LUA_TABLIBNAME	"table"
int (luaopen_table) (LuaThread *L);

#define LUA_IOLIBNAME	"io"
int (luaopen_io) (LuaThread *L);

#define LUA_OSLIBNAME	"os"
int (luaopen_os) (LuaThread *L);

#define LUA_STRLIBNAME	"string"
int (luaopen_string) (LuaThread *L);

#define LUA_BITLIBNAME	"bit32"
int (luaopen_bit32) (LuaThread *L);

#define LUA_MATHLIBNAME	"math"
int (luaopen_math) (LuaThread *L);

#define LUA_DBLIBNAME	"debug"
int (luaopen_debug) (LuaThread *L);

#define LUA_LOADLIBNAME	"package"
int (luaopen_package) (LuaThread *L);

#define LUA_TESTLIBNAME "T"
int (luaopen_test) (LuaThread *L);

/* open all previous libraries */
void (luaL_openlibs) (LuaThread *L);



#if !defined(assert)
#define assert(x)	((void)0)
#endif


#endif
