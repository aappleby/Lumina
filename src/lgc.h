/*
** $Id: lgc.h,v 2.52 2011/10/03 17:54:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h

#include "LuaGlobals.h"

#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/

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



/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSatomic	1
#define GCSsweepstring	2
#define GCSsweepudata	3
#define GCSsweep	4
#define GCSpause	5


#define issweepphase(g) (GCSsweepstring <= (g)->gcstate && (g)->gcstate <= GCSsweep)

#define isgenerational(g)	(g->gckind == KGC_GEN)

/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a non-generational collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again. During a generational collection, the
** invariant must be kept all times.
*/
#define keepinvariant(g)  (isgenerational(g) || (g->gcstate <= GCSatomic))


#define luaC_condGC(L,c) {if (thread_G->GCdebt > 0) {c;};}

void luaC_step();

void luaC_checkGC();


#define luaC_barrier(p,v) { if ((v)->isWhite() && (p)->isBlack())	luaC_barrier_(p,v->getObject()); }
#define luaC_barrierback(p,v) { if ((v)->isWhite() && (p)->isBlack()) luaC_barrierback_(p); }
#define luaC_objbarrier(L,p,o)  { if ((o)->isWhite() && (p)->isBlack()) luaC_barrier_(p,o); }
#define luaC_objbarrierback(L,p,o)  { if ((o)->isWhite() && (p)->isBlack()) luaC_barrierback_(p); }
#define luaC_barrierproto(p,c) { if ((p)->isBlack()) luaC_barrierproto_(p,c); }

void luaC_freeallobjects ();
void luaC_step ();
void luaC_forcestep ();
void luaC_runtilstate (int statesmask);
void luaC_fullgc (int isemergency);
void luaC_barrier_ (LuaObject *o, LuaObject *v);
void luaC_barrierback_ (LuaObject *o);
void luaC_barrierproto_ (Proto *p, Closure *c);
void luaC_checkfinalizer (LuaObject *o, Table *mt);
void luaC_checkupvalcolor (global_State *g, UpVal *uv);
void luaC_changemode (lua_State *L, int mode);

#endif
