#pragma once
#include "stdint.h"

class LuaBase;
class TString;
class lua_State;
class global_State;
class Closure;

extern __declspec(thread) lua_State* thread_L;
//extern __declspec(thread) global_State* thread_G;

class LuaScope {
public:
  LuaScope(lua_State* L) {
    oldState = thread_L;
    thread_L = L;
  }
  ~LuaScope() {
    thread_L = oldState;
  }

  lua_State* oldState;
};

class LuaGlobalScope {
public:
  LuaGlobalScope(lua_State* L) {
    oldState = thread_L;
    thread_L = L;
    //thread_G = thread_L->l_G;
  }
  ~LuaGlobalScope() {
    thread_L = oldState;
    //thread_G = thread_L->l_G;
  }

  lua_State* oldState;
};

#define THREAD_CHECK(A)  assert(thread_L == A);
//#define THREAD_CHECK(A)  assert((thread_L == A) && (thread_G == A->l_G));
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
