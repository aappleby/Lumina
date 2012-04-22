/*
** $Id: ldo.h,v 2.20 2011/11/29 15:55:08 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


#define savestack(L,p)		((p) - L->stack_.begin())
#define restorestack(L,n)	(L->stack_.begin() + (n))


/* type of protected functions, to be ran by `runprotected' */
typedef void (*Pfunc) (lua_State *L, void *ud);

int luaD_protectedparser (lua_State *L, ZIO *z, const char *name, const char *mode);
void luaD_hook (lua_State *L, int event, int line);
int luaD_precall (lua_State *L, StkId func, int nresults);
void luaD_call (lua_State *L, StkId func, int nResults,
                                        int allowyield);
int luaD_postcall (lua_State *L, StkId firstResult);

l_noret luaD_throw (int errcode);

#endif

