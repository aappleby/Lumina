#pragma once
#include "stdint.h"

class LuaObject;
class TString;
class lua_State;
class global_State;
class Closure;
class stringtable;

extern __declspec(thread) lua_State* thread_L;
extern __declspec(thread) global_State* thread_G;

stringtable* getGlobalStringtable();

class LuaScope {
public:
  LuaScope(lua_State* L);
  ~LuaScope();

  lua_State* oldState;
};

class LuaGlobalScope {
public:
  LuaGlobalScope(lua_State* L);
  ~LuaGlobalScope();

  lua_State* oldState;
};

//#define THREAD_CHECK(A)  assert(thread_L == A);
#define THREAD_CHECK(A)  assert((thread_L == A) && (thread_G == A->l_G));
#define THREAD_CHANGE(A) LuaScope luascope(A);
#define GLOBAL_CHANGE(A) LuaGlobalScope luascope(A);
//#define THREAD_CHECK(A)
//#define THREAD_CHANGE(A)


#define LUA_NUMBER_DOUBLE
#define LUA_NUMBER	double

/*
@@ LUA_INTEGER is the integral type used by lua_pushinteger/lua_tointeger.
** CHANGE that if ptrdiff_t is not adequate on your machine. (On most
** machines, ptrdiff_t gives a good choice between int or long.)
*/
#define LUA_INTEGER	ptrdiff_t

/*
@@ LUA_UNSIGNED is the integral type used by lua_pushunsigned/lua_tounsigned.
** It must have at least 32 bits.
*/
#define LUA_UNSIGNED	uint32_t


typedef int (*lua_CFunction) (lua_State *L);



/* type of numbers in Lua */
typedef LUA_NUMBER lua_Number;


/* type for integer functions */
typedef LUA_INTEGER lua_Integer;

/* unsigned integer type */
typedef LUA_UNSIGNED lua_Unsigned;




/*
@@ LUA_INT32 is an signed integer with exactly 32 bits.
@@ LUAI_UMEM is an unsigned integer big enough to count the total
@* memory used by Lua.
@@ LUAI_MEM is a signed integer big enough to count the total memory
@* used by Lua.
** CHANGE here if for some weird reason the default definitions are not
** good enough for your machine. Probably you do not need to change
** this.
*/
#define LUAI_MEM	ptrdiff_t



/*
** non-return type
*/
#if defined(__GNUC__)
#define l_noret		void __attribute__((noreturn))
#elif defined(_MSC_VER)
#define l_noret		void __declspec(noreturn)
#else
#define l_noret		void
#endif


typedef LUAI_MEM l_mem;



/*
** type for virtual-machine instructions
** must be an unsigned with (at least) 4 bytes (see details in lopcodes.h)
*/
typedef uint32_t Instruction;


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

  LUA_ALLTAGS = LUA_TDEADKEY+1,
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
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TUPVAL+2)

