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


#define tostring(L,o) ((o)->isString() || (luaV_tostring(L, o)))

#define tonumber(o,n)	(o->isNumber() || (((o) = luaV_tonumber(o,n)) != NULL))

#define equalobj(L,o1,o2)  ((o1->tt_ ==  o2->tt_) && luaV_equalobj_(L, o1, o2))

#define luaV_rawequalobj(t1,t2)  (((t1)->tt_ == (t2)->tt_) && ((t1)->bytes == (t2)->bytes))


/* not to called directly */
int luaV_equalobj_ (lua_State *L, const TValue *t1, const TValue *t2);
int luaV_equalobj2_ (const TValue *t1, const TValue *t2);


int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r);
int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r);
const TValue *luaV_tonumber (const TValue *obj, TValue *n);
int luaV_tostring (lua_State *L, StkId obj);
void luaV_gettable (lua_State *L, const TValue *t, TValue *key, StkId val);
void luaV_settable (lua_State *L, const TValue *t, TValue *key, StkId val);
void luaV_finishOp (lua_State *L);
void luaV_execute (lua_State *L);
void luaV_concat (lua_State *L, int total);
void luaV_arith (lua_State *L, StkId ra, const TValue *rb,
                           const TValue *rc, TMS op);
void luaV_objlen (lua_State *L, StkId ra, const TValue *rb);

#endif
