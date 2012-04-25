/*
** $Id: ltm.h,v 2.11 2011/02/28 17:32:10 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"
#include "LuaTypes.h"

const char* ttypename(int tag);
const char* objtypename(const LuaValue* v);

LuaValue luaT_gettmbyobj2 (LuaValue v, TMS event);
LuaValue fasttm2 ( LuaTable* table, TMS tag);

#endif
