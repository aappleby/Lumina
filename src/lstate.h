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


struct lua_longjmp;  /* defined in ldo.c */




#define isLua(ci)	((ci)->callstatus & CIST_LUA)


#define G(L)	(L->l_G)

#include "LuaObject.h"

/* macros to convert a LuaObject into a specific value */
#define gco2ts(o)	check_exp((o)->isString(), dynamic_cast<TString*>(o))
#define gco2u(o)	check_exp((o)->isUserdata(), dynamic_cast<Udata*>(o))
#define gco2cl(o)	check_exp((o)->isClosure(), dynamic_cast<Closure*>(o))
#define gco2t(o)	check_exp((o)->isTable(), dynamic_cast<Table*>(o))
#define gco2p(o)	check_exp((o)->isProto(), dynamic_cast<Proto*>(o))
#define gco2th(o)	check_exp((o)->isThread(), dynamic_cast<lua_State*>(o))

/* actual number of total bytes allocated */
#define gettotalbytes(g)	((g)->totalbytes + (g)->GCdebt)

void luaE_setdebt (global_State *g, l_mem debt);
void luaE_freethread (lua_State *L, lua_State *L1);
CallInfo *luaE_extendCI (lua_State *L);


#endif

