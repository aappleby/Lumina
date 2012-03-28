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
class Table;



#include "LuaValue.h"


#include "LuaString.h"







#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

#include "LuaTable.h"


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


extern const TValue luaO_nilobject_;


int luaO_int2fb (unsigned int x);
int luaO_fb2int (int x);
int luaO_ceillog2 (unsigned int x);
lua_Number luaO_arith (int op, lua_Number v1, lua_Number v2);
int luaO_str2d (const char *s, size_t len, lua_Number *result);
int luaO_hexavalue (int c);
const char *luaO_pushvfstring (const char *fmt, va_list argp);
const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
void luaO_chunkid (char *out, const char *source, size_t len);


#endif

