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


#define G(L)	(L->l_G)

#include "LuaObject.h"

LuaStackFrame *luaE_extendCI (LuaThread *L);


#endif

