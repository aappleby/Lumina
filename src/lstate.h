/*
** $Id: lstate.h,v 2.74 2011/09/30 12:45:07 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


struct lua_longjmp;  /* defined in ldo.c */




#define G(L)	(L->l_G)

#include "LuaObject.h"

CallInfo *luaE_extendCI (lua_State *L);


#endif

