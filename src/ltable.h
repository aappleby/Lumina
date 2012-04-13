/*
** $Id: ltable.h,v 2.16 2011/08/17 20:26:47 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


void luaH_setint (Table *t, int key, TValue *value);

// these return NULL instead of luaO_nilobject
const TValue *luaH_get2 (Table *t, const TValue *key);


void luaH_set2(Table* t, TValue key, TValue val);

#endif
