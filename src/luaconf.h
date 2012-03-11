/*
** $Id: luaconf.h,v 1.170 2011/12/06 16:58:36 roberto Exp $
** Configuration file for Lua
** See Copyright Notice in lua.h
*/


#ifndef lconfig_h
#define lconfig_h

#include <limits.h>
#include <stddef.h>

#include "LuaTypes.h"


/*
** ==================================================================
** Search for "@@" to find all configurable definitions.
** ===================================================================
*/


/*
@@ LUA_ANSI controls the use of non-ansi features.
** CHANGE it (define it) if you want Lua to avoid the use of any
** non-ansi feature or library.
*/
#if !defined(LUA_ANSI) && defined(__STRICT_ANSI__)
#define LUA_ANSI
#endif


#if !defined(LUA_ANSI) && defined(_WIN32) && !defined(_WIN32_WCE)
#define LUA_WIN		/* enable goodies for regular Windows platforms */
#endif

#if defined(LUA_WIN)
#define LUA_DL_DLL
#define LUA_USE_AFORMAT		/* assume 'printf' handles 'aA' specifiers */
#define LUA_USE_LONGLONG	/* assume support for long long */
#endif



#if defined(LUA_USE_LINUX)
#define LUA_USE_POSIX
#define LUA_USE_DLOPEN		/* needs an extra library: -ldl */
#define LUA_USE_READLINE	/* needs some extra libraries */
#define LUA_USE_STRTODHEX	/* assume 'strtod' handles hexa formats */
#define LUA_USE_AFORMAT		/* assume 'printf' handles 'aA' specifiers */
#define LUA_USE_LONGLONG	/* assume support for long long */
#endif

#if defined(LUA_USE_MACOSX)
#define LUA_USE_POSIX
#define LUA_USE_DLOPEN		/* does not need -ldl */
#define LUA_USE_READLINE	/* needs an extra library: -lreadline */
#define LUA_USE_STRTODHEX	/* assume 'strtod' handles hexa formats */
#define LUA_USE_AFORMAT		/* assume 'printf' handles 'aA' specifiers */
#define LUA_USE_LONGLONG	/* assume support for long long */
#endif



/*
@@ LUA_USE_POSIX includes all functionality listed as X/Open System
@* Interfaces Extension (XSI).
** CHANGE it (define it) if your system is XSI compatible.
*/
#if defined(LUA_USE_POSIX)
#define LUA_USE_MKSTEMP
#define LUA_USE_ISATTY
#define LUA_USE_POPEN
#define LUA_USE_ULONGJMP
#define LUA_USE_GMTIME_R
#endif



/*
@@ LUA_PATH_DEFAULT is the default path that Lua uses to look for
@* Lua libraries.
@@ LUA_CPATH_DEFAULT is the default path that Lua uses to look for
@* C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/
#if defined(_WIN32)	/* { */
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define LUA_LDIR	"!\\lua\\"
#define LUA_CDIR	"!\\"
#define LUA_PATH_DEFAULT  \
		LUA_LDIR"?.lua;"  LUA_LDIR"?\\init.lua;" \
		LUA_CDIR"?.lua;"  LUA_CDIR"?\\init.lua;" ".\\?.lua"
#define LUA_CPATH_DEFAULT \
		LUA_CDIR"?.dll;" LUA_CDIR"loadall.dll;" ".\\?.dll"

#else			/* }{ */

#define LUA_VDIR	LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "/"
#define LUA_ROOT	"/usr/local/"
#define LUA_LDIR	LUA_ROOT "share/lua/" LUA_VDIR
#define LUA_CDIR	LUA_ROOT "lib/lua/" LUA_VDIR
#define LUA_PATH_DEFAULT  \
		LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua;" \
		LUA_CDIR"?.lua;"  LUA_CDIR"?/init.lua;" "./?.lua"
#define LUA_CPATH_DEFAULT \
		LUA_CDIR"?.so;" LUA_CDIR"loadall.so;" "./?.so"
#endif			/* } */


/*
@@ LUA_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Lua automatically uses "\".)
*/
#if defined(_WIN32)
#define LUA_DIRSEP	"\\"
#else
#define LUA_DIRSEP	"/"
#endif


/*
@@ LUA_ENV is the name of the variable that holds the current
@@ environment, used to access global names.
** CHANGE it if you do not like this name.
*/
#define LUA_ENV		"_ENV"


/*
@@ LUA_QL describes how error messages quote program elements.
** CHANGE it if you want a different appearance.
*/
#define LUA_QL(x)	"'" x "'"
#define LUA_QS		LUA_QL("%s")


/*
@@ LUA_IDSIZE gives the maximum size for the description of the source
@* of a function in debug information.
** CHANGE it if you want a different size.
*/
#define LUA_IDSIZE	60


/*
@@ luai_writestring/luai_writeline define how 'print' prints its results.
** They are only used in libraries and the stand-alone program. (The #if
** avoids including 'stdio.h' everywhere.)
*/
#if defined(LUA_LIB) || defined(lua_c)
#include <stdio.h>
#define luai_writestring(s,l)	fwrite((s), sizeof(char), (l), stdout)
#define luai_writeline()	(luai_writestring("\n", 1), fflush(stdout))
#endif

/*
@@ luai_writestringerror defines how to print error messages.
** (A format string with one argument is enough for Lua...)
*/
#define luai_writestringerror(s,p) \
	(fprintf(stderr, (s), (p)), fflush(stderr))


/* }================================================================== */

/*
@@ LUAI_MAXSTACK limits the size of the Lua stack.
** CHANGE it if you need a different limit. This limit is arbitrary;
** its only purpose is to stop Lua to consume unlimited stack
** space (and to reserve some numbers for pseudo-indices).
*/
#define LUAI_MAXSTACK		1000000

/* reserve some space for error handling */
#define LUAI_FIRSTPSEUDOIDX	(-LUAI_MAXSTACK - 1000)




/*
@@ LUAL_BUFFERSIZE is the buffer size used by the lauxlib buffer system.
** CHANGE it if it uses too much C-stack space.
*/
#define LUAL_BUFFERSIZE		BUFSIZ


/*
@@ LUA_NUMBER_SCAN is the format for reading numbers.
@@ LUA_NUMBER_FMT is the format for writing numbers.
@@ lua_number2str converts a number to a string.
@@ LUAI_MAXNUMBER2STR is maximum size of previous conversion.
*/
#define LUA_NUMBER_SCAN		"%lf"
#define LUA_NUMBER_FMT		"%.14g"
#define lua_number2str(s,n)	sprintf((s), LUA_NUMBER_FMT, (n))
#define LUAI_MAXNUMBER2STR	32 /* 16 digits, sign, point, and \0 */


/*
@@ lua_str2number converts a decimal numeric string to a number.
@@ lua_strx2number converts an hexadecimal numeric string to a number.
** In C99, 'strtod' do both conversions. C89, however, has no function
** to convert floating hexadecimal strings to numbers. For these
** systems, you can leave 'lua_strx2number' undefined and Lua will
** provide its own implementation.
*/
#define lua_str2number(s,p)	strtod((s), (p))

#if defined(LUA_USE_STRTODHEX)
#define lua_strx2number(s,p)	strtod((s), (p))
#endif


/*
@@ The luai_num* macros define the primitive operations over numbers.
*/

/* the following operations need the math library */
#if defined(lobject_c) || defined(lvm_c)
#include <math.h>
#define luai_nummod(L,a,b)	((a) - floor((a)/(b))*(b))
#define luai_numpow(L,a,b)	(pow(a,b))
#endif

/* these are quite standard operations */
#if defined(LUA_CORE)
#define luai_numadd(L,a,b)	((a)+(b))
#define luai_numsub(L,a,b)	((a)-(b))
#define luai_nummul(L,a,b)	((a)*(b))
#define luai_numdiv(L,a,b)	((a)/(b))
#define luai_numunm(L,a)	(-(a))
#define luai_numeq(a,b)		((a)==(b))
#define luai_numlt(L,a,b)	((a)<(b))
#define luai_numle(L,a,b)	((a)<=(b))
#define luai_numisnan(L,a)	(!luai_numeq((a), (a)))
#endif



/* }================================================================== */


/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/



#endif

