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
const char* objtypename(const TValue* v);

const TValue *luaT_gettm (Table *events, TMS event, TString *ename);

const TValue *luaT_gettmbyobj (const TValue *o, TMS event);
TValue luaT_gettmbyobj2 (TValue v, TMS event);

void luaT_init ();

const TValue* fasttm  ( Table* table, TMS tag);
TValue fasttm2 ( Table* table, TMS tag);

#endif
