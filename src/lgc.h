/*
** $Id: lgc.h,v 2.52 2011/10/03 17:54:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


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
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSatomic	1
#define GCSsweepstring	2
#define GCSsweepudata	3
#define GCSsweep	4
#define GCSpause	5


#define issweepphase(g)  \
	(GCSsweepstring <= (g)->gcstate && (g)->gcstate <= GCSsweep)

#define isgenerational(g)	((g)->gckind == KGC_GEN)

/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a non-generational collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again. During a generational collection, the
** invariant must be kept all times.
*/
#define keepinvariant(g)  (isgenerational(g) || g->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(uint8_t, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))


#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


#define iswhite(x)      testbits((x)->marked, WHITEBITS)
#define isblack(x)      testbit((x)->marked, BLACKBIT)
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))

#define isold(x)	testbit((x)->marked, OLDBIT)

/* MOVE OLD rule: whenever an object is moved to the beginning of
   a GC list, its old bit must be cleared */
#define resetoldbit(o)	resetbit((o)->marked, OLDBIT)

#define otherwhite()	(thread_G->currentwhite ^ WHITEBITS)
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
#define isdead(v)	isdeadm(otherwhite(), (v)->marked)

#define changewhite(x)	((x)->marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->marked, BLACKBIT)

#define valiswhite(x)	(iscollectable(x) && iswhite(gcvalue(x)))

#define luaC_white(g)	cast(uint8_t, (g)->currentwhite & WHITEBITS)


#define luaC_condGC(L,c) {if (thread_G->GCdebt > 0) {c;};}

void luaC_step();

void luaC_checkGC();


#define luaC_barrier(p,v) { if (valiswhite(v) && isblack(obj2gco(p)))	luaC_barrier_(obj2gco(p),gcvalue(v)); }
#define luaC_barrierback(p,v) { if (valiswhite(v) && isblack(obj2gco(p))) luaC_barrierback_(p); }

#define luaC_objbarrier(L,p,o)  \
	{ if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) \
		luaC_barrier_(obj2gco(p),obj2gco(o)); }

#define luaC_objbarrierback(L,p,o)  \
   { if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) luaC_barrierback_(p); }

#define luaC_barrierproto(p,c) \
   { if (isblack(obj2gco(p))) luaC_barrierproto_(p,c); }

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
