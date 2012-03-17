#pragma once
#include "LuaTypes.h"

/*
** basic types
*/
enum LuaTag {
  LUA_TNONE          = -1,
  LUA_TNIL           = 0,
  LUA_TBOOLEAN       = 1,
  LUA_TLIGHTUSERDATA = 2,
  LUA_TNUMBER        = 3,
  LUA_TSTRING        = 4,
  LUA_TTABLE         = 5,
  LUA_TFUNCTION      = 6,
  LUA_TUSERDATA      = 7,
  LUA_TTHREAD        = 8,
  LUA_NUMTAGS        = 9,

  // non-values
  LUA_TPROTO = 9,
  LUA_TUPVAL = 10,
  LUA_TDEADKEY = 11,
};

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TUPVAL+2)

struct TValue {
  union {
    LuaBase *gc;    /* collectable objects */
    void *p;         /* light userdata */
    int32_t b;           /* booleans */
    lua_CFunction f; /* light C functions */
    lua_Number n;    /* numbers */
    uint64_t bytes;
  };
  int32_t tt_;

  int32_t rawtype() const { return tt_; }
  int32_t tagtype() const { return tt_ & 0x3f; }
  int32_t basetype() const { return tt_ & 0x0f; }


  void setBool  (int x)     { b = x; tt_ = LUA_TBOOLEAN; }
  void setValue (TValue* x) { bytes = x->bytes; tt_ = x->tt_; }

  TString* asString() { return reinterpret_cast<TString*>(gc); }
};





/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)

/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/* raw type tag of a TValue */
#define rttype(o)	((o)->tt_)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)


/* type tag of a TValue with no variants (bits 0-3) */
#define ttypenv(o)	(rttype(o) & 0x0F)


/* Macros to test type */
#define checktag(o,t)		      (rttype(o) == (t))
#define ttisnumber(o)		      checktag((o), LUA_TNUMBER)
#define ttisnil(o)		        checktag((o), LUA_TNIL)
#define ttisboolean(o)		    checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		      checktag((o), ctb(LUA_TSTRING))
#define ttistable(o)		      checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		    (ttypenv(o) == LUA_TFUNCTION)
#define ttisclosure(o)		    ((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		    checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		    checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		        checktag((o), LUA_TLCF)
#define ttisuserdata(o)		    checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		      checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		    checktag((o), LUA_TDEADKEY)

#define ttisequal(o1,o2)	    (rttype(o1) == rttype(o2))

/* Macros to access values */
#define nvalue(o)	            check_exp(ttisnumber(o), (o)->n)
#define gcvalue(o)	          check_exp(iscollectable(o), (o)->gc)
#define pvalue(o)	            check_exp(ttislightuserdata(o), (o)->p)
#define tsvalue(o)	          check_exp(ttisstring(o), reinterpret_cast<TString*>((o)->gc))
#define uvalue(o)	            check_exp(ttisuserdata(o), reinterpret_cast<Udata*>((o)->gc))
#define clvalue(o)	          check_exp(ttisclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define clLvalue(o)	          check_exp(ttisLclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define clCvalue(o)	          check_exp(ttisCclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define fvalue(o)	            check_exp(ttislcf(o), (o)->f)
#define hvalue(o)	            check_exp(ttistable(o), reinterpret_cast<Table*>((o)->gc))
#define bvalue(o)	            check_exp(ttisboolean(o), (o)->b)
#define thvalue(o)	          check_exp(ttisthread(o), reinterpret_cast<lua_State*>((o)->gc))

/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	        check_exp(ttisdeadkey(o), cast(void *, (o)->gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttypenv(obj) == gcvalue(obj)->tt)

#define checkliveness(g,obj) assert(!iscollectable(obj) || (righttt(obj) && !isdead(g,gcvalue(obj))))


/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setnvalue(obj,x) \
  { TValue *io=(obj); io->n=(x); io->tt_=LUA_TNUMBER; }

#define changenvalue(o,x)	check_exp(ttisnumber(o), (o)->n=(x))

#define setnilvalue(obj) settt_(obj, LUA_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); io->f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); io->p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); io->b=(x); settt_(io, LUA_TBOOLEAN); }

#define setgcovalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); LuaBase *i_g=(x); \
    io->gc=i_g; settt_(io, ctb(gch(i_g)->tt)); }

#define setsvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TSTRING)); \
    checkliveness(G(L),io); }

#define setuvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(G(L),io); }

#define setthvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(G(L),io); }

#define setclLvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(G(L),io); }

#define setclCvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(G(L),io); }

#define sethvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(G(L),io); }

#define setptvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); \
    io->gc=cast(LuaBase *, (x)); settt_(io, ctb(LUA_TPROTO)); \
    checkliveness(G(L),io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)


// can't remove L here yet
#define setobj(L,obj1,obj2) \
	{ THREAD_CHECK(L); const TValue *io2=(obj2); TValue *io1=(obj1); \
	  io1->bytes = io2->bytes; io1->tt_ = io2->tt_; \
	  checkliveness(G(L),io1); }


#define luai_checknum(L,o,c)	{ /* empty */ }

