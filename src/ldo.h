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


/* type of protected functions, to be ran by `runprotected' */
typedef void (*Pfunc) (LuaThread *L, void *ud);

int luaD_protectedparser (LuaThread *L, ZIO *z, const char *name, const char *mode);
void luaD_hook (LuaThread *L, int event, int line);
int luaD_precall (LuaThread *L, StkId func, int nargs, int nresults);
void luaD_call (LuaThread *L, StkId func, int nargs, int nresults, int allowyield);
int luaD_postcall (LuaThread *L, StkId firstResult);

l_noret luaD_throw (int errcode);

#endif

