/*
** $Id: lstring.h,v 1.46 2010/04/05 16:26:37 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define sizestring(s)	(sizeof(TString)+((s)->len+1)*sizeof(char))

#define sizeudata(u)	(sizeof(Udata)+(u)->len)

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))

#define luaS_fix(s)	l_setbit((s)->marked, FIXEDBIT)


/*
** as all string are internalized, string equality becomes
** pointer equality
*/
#define eqstr(a,b)	((a) == (b))

void luaS_resize (lua_State *L, int newsize);
Udata *luaS_newudata (lua_State *L, size_t s, Table *e);
TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
TString *luaS_new (lua_State *L, const char *str);


#endif
