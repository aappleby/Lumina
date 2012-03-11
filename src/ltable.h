/*
** $Id: ltable.h,v 2.16 2011/08/17 20:26:47 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


#define gnode(t,i)	(&(t)->node[i])
#define gkey(n)   (reinterpret_cast<TValue*>(&(n)->i_key))
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->next)

#define invalidateTMcache(t)	((t)->flags = 0)


const TValue *luaH_getint (Table *t, int key);
void luaH_setint (lua_State *L, Table *t, int key, TValue *value);
const TValue *luaH_getstr (Table *t, TString *key);
const TValue *luaH_get (Table *t, const TValue *key);
TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key);
TValue *luaH_set (lua_State *L, Table *t, const TValue *key);
Table *luaH_new (lua_State *L);
void luaH_resize (lua_State *L, Table *t, int nasize, int nhsize);
void luaH_resizearray (lua_State *L, Table *t, int nasize);
void luaH_free (lua_State *L, Table *t);
int luaH_next (lua_State *L, Table *t, StkId key);
int luaH_getn (Table *t);


#if defined(LUA_DEBUG)
Node *luaH_mainposition (const Table *t, const TValue *key);
int luaH_isdummy (Node *n);
#endif


#endif
