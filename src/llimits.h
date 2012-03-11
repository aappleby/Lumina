/*
** $Id: llimits.h,v 1.95 2011/12/06 16:58:36 roberto Exp $
** Limits, basic types, and some other `installation-dependent' definitions
** See Copyright Notice in lua.h
*/

#ifndef llimits_h
#define llimits_h


#include <limits.h>
#include <stddef.h>
#include "stdint.h"

#include "lua.h"


typedef LUAI_MEM l_mem;

#define MAX_SIZET	((size_t)(~(size_t)0)-2)
#define MAX_LUMEM	((size_t)(~(size_t)0)-2)
#define MAX_INT (INT_MAX-2)  /* maximum value of an int (-2 for safety) */


#define check_exp(c,e)		(assert(c), (e))
#define luai_apicheck(L,e)	assert(e)
#define api_check(l,e,msg)	luai_apicheck(l,(e) && msg)
#define UNUSED(x)	((void)(x))	/* to avoid warnings */


#define cast(t, exp)	((t)(exp))

#define cast_byte(i)	cast(uint8_t, (i))
#define cast_num(i)	cast(lua_Number, (i))
#define cast_int(i)	cast(int, (i))
#define cast_uchar(i)	cast(unsigned char, (i))


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



/*
** maximum depth for nested C calls and syntactical nested non-terminals
** in a program. (Value must fit in an unsigned short int.)
*/
#if !defined(LUAI_MAXCCALLS)
#define LUAI_MAXCCALLS		200
#endif

/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in an unsigned char.)
*/
#define MAXUPVAL	UCHAR_MAX


/*
** type for virtual-machine instructions
** must be an unsigned with (at least) 4 bytes (see details in lopcodes.h)
*/
typedef uint32_t Instruction;



/* maximum stack for a Lua function */
#define MAXSTACK	250



/* minimum size for the string table (must be power of 2) */
#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE	32
#endif


/* minimum size for string buffer */
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER	32
#endif


#if !defined(lua_lock)
#define lua_lock(L)     ((void) 0)
#define lua_unlock(L)   ((void) 0)
#endif

#if !defined(luai_threadyield)
#define luai_threadyield(L)     {lua_unlock(L); lua_lock(L);}
#endif


/*
** lua_number2int is a macro to convert lua_Number to int.
** lua_number2integer is a macro to convert lua_Number to lua_Integer.
** lua_number2unsigned is a macro to convert a lua_Number to a lua_Unsigned.
** lua_unsigned2number is a macro to convert a lua_Unsigned to a lua_Number.
** luai_hashnum is a macro to hash a lua_Number value into an integer.
** The hash must be deterministic and give reasonable values for
** both small and large values (outside the range of integers).
*/

#define lua_number2int(i,n)	       ((i)=(int32_t)(n))
#define lua_number2integer(i,n)	   ((i)=(lua_Integer)(n))
#define lua_number2unsigned(i,n)	 ((i)=(uint32_t)(n))
#define lua_unsigned2number(u)     ((double)(u))



#if defined(ltable_c) && !defined(luai_hashnum)

#include <float.h>
#include <math.h>

#define luai_hashnum(i,n) { int e;  \
  n = frexp(n, &e) * (lua_Number)(INT_MAX - DBL_MAX_EXP);  \
  lua_number2int(i, n); i += e; }

#endif


#endif
