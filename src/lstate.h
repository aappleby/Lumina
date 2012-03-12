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



/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */
#define KGC_GEN		2	/* generational collection */


/*
** information about a call
*/
class CallInfo {
public:
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  CallInfo *previous, *next;  /* dynamic call link */
  short nresults;  /* expected number of results from this function */
  uint8_t callstatus;
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      int ctx;  /* context info. in case of yields */
      lua_CFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      ptrdiff_t extra;
      uint8_t old_allowhook;
      uint8_t status;
    } c;
  } u;
};


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


/*
** `global state', shared by all threads of this state
*/
class global_State {
public:
  lua_Alloc frealloc;  /* function to reallocate memory */
  size_t totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  size_t lastmajormem;  /* memory in use after last major collection */
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  uint8_t currentwhite;
  uint8_t gcstate;  /* state of garbage collector */
  uint8_t gckind;  /* kind of GC running */
  uint8_t gcrunning;  /* true if GC is running */
  int sweepstrgc;  /* position of sweep in `strt' */
  LuaBase *allgc;  /* list of all collectable objects */
  LuaBase *finobj;  /* list of collectable objects with finalizers */
  LuaBase **sweepgc;  /* current position of sweep */
  LuaBase *gray;  /* list of gray objects */
  LuaBase *grayagain;  /* list of objects to be traversed atomically */
  LuaBase *weak;  /* list of tables with weak values */
  LuaBase *ephemeron;  /* list of ephemeron tables (weak keys) */
  LuaBase *allweak;  /* list of all-weak tables */
  LuaBase *tobefnz;  /* list of userdata to be GC */
  UpVal uvhead;  /* head of double-linked list of all open upvalues */
  Mbuffer buff;  /* temporary buffer for string concatenation */
  int gcpause;  /* size of pause between successive GCs */
  int gcmajorinc;  /* how much to wait for a major GC (only in gen. mode) */
  int gcstepmul;  /* GC `granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names */
  Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
};

class global_State;
class CallInfo;

/*
** `per thread' state
*/
class lua_State : public LuaBase {
public:
  uint8_t status;
  StkId top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  TValue* stack_last;  /* last free slot in the stack */
  TValue* stack;  /* stack base */
  int stacksize;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  uint8_t hookmask;
  uint8_t allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  LuaBase *openupval;  /* list of open upvalues in this stack */
  LuaBase *gclist;
  lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
};

#define G(L)	(L->l_G)

#include "LuaBase.h"

#define gch(o)		(o)

/* macros to convert a LuaBase into a specific value */
#define gco2ts(o)	check_exp((o)->tt == LUA_TSTRING, reinterpret_cast<TString*>(o))
#define rawgco2u(o)	check_exp((o)->tt == LUA_TUSERDATA, reinterpret_cast<Udata*>(o))

#define gco2u(o)	(rawgco2u(o))

#define gco2cl(o)	check_exp((o)->tt == LUA_TFUNCTION, reinterpret_cast<Closure*>(o))
#define gco2t(o)	check_exp((o)->tt == LUA_TTABLE, reinterpret_cast<Table*>(o))
#define gco2p(o)	check_exp((o)->tt == LUA_TPROTO, reinterpret_cast<Proto*>(o))
#define gco2uv(o)	check_exp((o)->tt == LUA_TUPVAL, reinterpret_cast<UpVal*>(o))
#define gco2th(o)	check_exp((o)->tt == LUA_TTHREAD, reinterpret_cast<lua_State*>(o))

/* macro to convert any Lua object into a LuaBase */
#define obj2gco(v)	(cast(LuaBase *, (v)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	((g)->totalbytes + (g)->GCdebt)

void luaE_setdebt (global_State *g, l_mem debt);
void luaE_freethread (lua_State *L, lua_State *L1);
CallInfo *luaE_extendCI (lua_State *L);
void luaE_freeCI (lua_State *L);


#endif

