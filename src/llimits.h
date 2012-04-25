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

#include "LuaTypes.h"


#define MAX_SIZET	((size_t)(~(size_t)0)-2)
#define MAX_LUMEM	((size_t)(~(size_t)0)-2)
#define MAX_INT (INT_MAX-2)  /* maximum value of an int (-2 for safety) */


#define check_exp(c,e)		(assert(c), (e))
#define api_check(e,msg)	assert((e) && msg)
#define UNUSED(x)	((void)(x))	/* to avoid warnings */


#define cast(t, exp)	((t)(exp))

#define cast_byte(i)	cast(uint8_t, (i))
#define cast_num(i)	cast(double, (i))
#define cast_int(i)	cast(int, (i))
#define cast_uchar(i)	cast(unsigned char, (i))



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



/* maximum stack for a Lua function */
#define MAXSTACK	250



/* minimum size for string buffer */
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER	32
#endif



/*
** lua_number2int is a macro to convert double to int.
** lua_number2integer is a macro to convert double to ptrdiff_t.
** lua_number2unsigned is a macro to convert a double to a uint32_t.
** lua_unsigned2number is a macro to convert a uint32_t to a double.
** The hash must be deterministic and give reasonable values for
** both small and large values (outside the range of integers).
*/

#define lua_number2int(i,n)	       ((i)=(int32_t)(n))
#define lua_number2integer(i,n)	   ((i)=(ptrdiff_t)(n))
#define lua_number2unsigned(i,n)	 ((i)=(uint32_t)(n))
#define lua_unsigned2number(u)     ((double)(u))


#endif
