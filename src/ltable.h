/*
** $Id: ltable.h,v 2.16 2011/08/17 20:26:47 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


#define gnode(t,i)	((t)->getNode(i))

void luaH_setint (Table *t, int key, TValue *value);

// these return NULL instead of luaO_nilobject
const TValue *luaH_get2 (Table *t, const TValue *key);
const TValue *luaH_getint2 (Table *t, int key);


TValue *luaH_set (Table *t, const TValue *key);
void luaH_set2(Table* t, TValue key, TValue val);

int luaH_next (Table *t, StkId key);
int luaH_getn (Table *t);

#endif
