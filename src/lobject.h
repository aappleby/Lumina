/*
** $Id: lobject.h,v 2.64 2011/10/31 17:48:22 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"

class Table;


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	LUA_NUMTAGS
#define LUA_TUPVAL	(LUA_NUMTAGS+1)
#define LUA_TDEADKEY	(LUA_NUMTAGS+2)

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TUPVAL+2)


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

union GCObject;



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
#define checktag(o,t)		(rttype(o) == (t))
#define ttisnumber(o)		checktag((o), LUA_TNUMBER)
#define ttisnil(o)		checktag((o), LUA_TNIL)
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		checktag((o), ctb(LUA_TSTRING))
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		(ttypenv(o) == LUA_TFUNCTION)
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		checktag((o), LUA_TLCF)
#define ttisuserdata(o)		checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)

#define ttisequal(o1,o2)	(rttype(o1) == rttype(o2))

/* Macros to access values */
#define nvalue(o)	check_exp(ttisnumber(o), (o)->n)
#define gcvalue(o)	check_exp(iscollectable(o), (o)->gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->p)
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->gc->ts)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisclosure(o), &(o)->gc->cl)
#define clLvalue(o)	check_exp(ttisLclosure(o), &(o)->gc->cl.l)
#define clCvalue(o)	check_exp(ttisCclosure(o), &(o)->gc->cl.c)
#define fvalue(o)	check_exp(ttislcf(o), (o)->f)
#define hvalue(o)	check_exp(ttistable(o), &(o)->gc->h)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->b)
#define thvalue(o)	check_exp(ttisthread(o), &(o)->gc->th)
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, (o)->gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttypenv(obj) == gcvalue(obj)->gch.tt)

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
  { TValue *io=(obj); GCObject *i_g=(x); \
    io->gc=i_g; settt_(io, ctb(gch(i_g)->tt)); }

#define setsvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TSTRING)); \
    checkliveness(G(L),io); }

#define setuvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(G(L),io); }

#define setthvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(G(L),io); }

#define setclLvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(G(L),io); }

#define setclCvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(G(L),io); }

#define sethvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(G(L),io); }

#define setptvalue(L,obj,x) \
  { TValue *io=(obj); \
    io->gc=cast(GCObject *, (x)); settt_(io, ctb(LUA_TPROTO)); \
    checkliveness(G(L),io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



#define setobj(L,obj1,obj2) \
	{ const TValue *io2=(obj2); TValue *io1=(obj1); \
	  io1->bytes = io2->bytes; io1->tt_ = io2->tt_; \
	  checkliveness(G(L),io1); }


/*
** different types of assignments, according to destination
*/

#define setsvalue2s	  setsvalue
#define sethvalue2s	  sethvalue
#define setptvalue2s	setptvalue
#define setsvalue2n	  setsvalue

#define luai_checknum(L,o,c)	{ /* empty */ }

/* }====================================================== */



/*
** {======================================================
** types and prototypes
** =======================================================
*/




struct TValue {
  union {
    GCObject *gc;    /* collectable objects */
    void *p;         /* light userdata */
    int b;           /* booleans */
    lua_CFunction f; /* light C functions */
    lua_Number n;    /* numbers */
    uint64_t bytes;
  };
  int tt_;
};


typedef TValue* StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
*/
__declspec(align(8)) union TString {
  struct {
    GCObject *next;
    uint8_t tt;
    uint8_t marked;
    uint8_t reserved;
    unsigned int hash;
    size_t len;  /* number of characters in string */
  } tsv;
};


/* get the actual string (array of bytes) from a TString */
#define getstr(ts)	cast(const char *, (ts) + 1)

/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(rawtsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
*/
__declspec(align(8)) union Udata {
  struct {
    GCObject *next;
    uint8_t tt;
    uint8_t marked;
    Table *metatable;
    Table *env;
    size_t len;  /* number of bytes */
  } uv;
};



/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  uint8_t instack;  /* whether it is in stack */
  uint8_t idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
typedef struct Proto {
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
  TValue *k;  /* constants used by the function */
  Instruction *code;
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  union Closure *cache;  /* last created closure with this prototype */
  TString  *source;  /* used for debug information */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of `k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of `p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  GCObject *gclist;
  uint8_t numparams;  /* number of fixed parameters */
  uint8_t is_vararg;
  uint8_t maxstacksize;  /* maximum stack used by this function */
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal {
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
  TValue *v;  /* points to stack or to its own value */
  union {
    TValue value;  /* the value (when closed) */
    struct {  /* double linked list (when open) */
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;


/*
** Closures
*/

#define ClosureHeader \
	GCObject *next; uint8_t tt; uint8_t marked; uint8_t isC; uint8_t nupvalues; GCObject *gclist

typedef struct CClosure {
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
  uint8_t isC;
  uint8_t nupvalues;
  GCObject *gclist;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


typedef struct LClosure {
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
  uint8_t isC;
  uint8_t nupvalues;
  GCObject *gclist;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

#include "LuaTable.h"



/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1ull<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;


LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC lua_Number luaO_arith (int op, lua_Number v1, lua_Number v2);
LUAI_FUNC int luaO_str2d (const char *s, size_t len, lua_Number *result);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

