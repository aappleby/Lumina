/*
** $Id: lvm.h,v 2.17 2011/05/31 18:27:56 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include "ldo.h"
#include "lobject.h"
#include "ltm.h"


/* not to called directly */
int luaV_equalobj_ (LuaThread *L, const LuaValue *t1, const LuaValue *t2);
int luaV_equalobj2_ (const LuaValue *t1, const LuaValue *t2);


int luaV_lessthan (LuaThread *L, const LuaValue *l, const LuaValue *r);
int luaV_lessequal (LuaThread *L, const LuaValue *l, const LuaValue *r);

int luaV_tostring (LuaValue* v);

void luaV_gettable (LuaThread *L, const LuaValue *t, LuaValue *key, StkId val);
LuaResult luaV_gettable2 (LuaThread *L, LuaValue table, LuaValue key, LuaValue& outResult);

void luaV_settable (LuaThread *L, const LuaValue *t, LuaValue *key, StkId val);
void luaV_finishOp (LuaThread *L);
void luaV_execute (LuaThread *L);
void luaV_concat (LuaThread *L, int total);
void luaV_arith (LuaThread *L, StkId ra, const LuaValue *rb,
                           const LuaValue *rc, TMS op);
void luaV_objlen (LuaThread *L, StkId ra, const LuaValue *rb);

#endif
