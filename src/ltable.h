/*
** $Id: ltable.h,v 2.16 2011/08/17 20:26:47 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


#define gnode(t,i)	((t)->getNode(i))

const TValue *luaH_getint (Table *t, int key);
void luaH_setint (Table *t, int key, TValue *value);

const TValue *luaH_getstr (Table *t, TString *key);
const TValue *luaH_get (Table *t, const TValue *key);
TValue *luaH_newkey (Table *t, const TValue *key);
TValue *luaH_set (Table *t, const TValue *key);
void luaH_resize (Table *t, int nasize, int nhsize);
void luaH_resizearray (Table *t, int nasize);
int luaH_next (Table *t, StkId key);
int luaH_getn (Table *t);
Node *luaH_mainposition (Table *t, const TValue *key);

#endif
