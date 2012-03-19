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


/*

** Some notes about garbage-collected objects:  All objects in Lua must
** be kept somehow accessible until being freed.
**
** Lua keeps most objects linked in list g->allgc. The link uses field
** 'next' of the common header.
**
** Strings are kept in several lists headed by the array g->strt.hash.
**
** Open upvalues are not subject to independent garbage collection. They
** are collected together with their respective threads. Lua keeps a
** double-linked list with all open upvalues (g->uvhead) so that it can
** mark objects referred by them. (They are always gray, so they must
** be remarked in the atomic step. Usually their contents would be marked
** when traversing the respective threads, but the thread may already be
** dead, while the upvalue is still accessible through closures.)
**
** Objects with finalizers are kept in the list g->finobj.
**
** The list g->tobefnz links all objects being finalized.

*/


struct lua_longjmp;  /* defined in ldo.c */


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */
#define KGC_GEN		2	/* generational collection */



/*
** Bits in CallInfo status
*/
#define CIST_LUA	(1<<0)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<1)	/* call is running a debug hook */
#define CIST_REENTRY	(1<<2)	/* call is running on same invocation of
                                   luaV_execute of previous call */
#define CIST_YIELDED	(1<<3)	/* call reentered after suspension */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_STAT	(1<<5)	/* call has an error status (pcall) */
#define CIST_TAIL	(1<<6)	/* call was tail called */


#define isLua(ci)	((ci)->callstatus & CIST_LUA)


#define G(L)	(L->l_G)

#include "LuaObject.h"

#define gch(o)		(o)

/* macros to convert a LuaObject into a specific value */
#define gco2ts(o)	check_exp((o)->tt == LUA_TSTRING, reinterpret_cast<TString*>(o))
#define rawgco2u(o)	check_exp((o)->tt == LUA_TUSERDATA, reinterpret_cast<Udata*>(o))

#define gco2u(o)	(rawgco2u(o))

#define gco2cl(o)	check_exp((o)->tt == LUA_TFUNCTION, reinterpret_cast<Closure*>(o))
#define gco2t(o)	check_exp((o)->tt == LUA_TTABLE, reinterpret_cast<Table*>(o))
#define gco2p(o)	check_exp((o)->tt == LUA_TPROTO, reinterpret_cast<Proto*>(o))
#define gco2uv(o)	check_exp((o)->tt == LUA_TUPVAL, reinterpret_cast<UpVal*>(o))
#define gco2th(o)	check_exp((o)->tt == LUA_TTHREAD, reinterpret_cast<lua_State*>(o))

/* macro to convert any Lua object into a LuaObject */
#define obj2gco(v)	(cast(LuaObject *, (v)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	((g)->totalbytes + (g)->GCdebt)

void luaE_setdebt (global_State *g, l_mem debt);
void luaE_freethread (lua_State *L, lua_State *L1);
CallInfo *luaE_extendCI (lua_State *L);
void luaE_freeCI (lua_State *L);


#endif

