/*
** $Id: ltests.h,v 2.33 2010/07/28 15:51:59 roberto Exp $
** Internal Header for Debugging of the Lua Implementation
** See Copyright Notice in lua.h
*/

#ifndef ltests_h
#define ltests_h


#include <stdlib.h>

/* do not use compatibility macros in Lua code */
#undef LUA_COMPAT_API

#define LUA_DEBUG

#undef NDEBUG
#include <assert.h>
#define lua_assert(c)           assert(c)


/* to avoid warnings, and to make sure value is really unused */
#define UNUSED(x)       (x=0, (void)(x))


/* memory allocator control variables */
typedef struct Memcontrol {
  unsigned long numblocks;
  unsigned long total;
  unsigned long maxmem;
  unsigned long memlimit;
  unsigned long objcount[LUA_NUMTAGS];
} Memcontrol;

extern Memcontrol l_memcontrol;


void *debug_realloc (void *block, size_t osize, size_t nsize);

typedef struct CallInfo *pCallInfo;

int lua_checkmemory (lua_State *L);

int luaB_opentests (lua_State *L);

#if defined(lua_c)
#define luaL_newstate()		lua_newstate(debug_realloc, &l_memcontrol)
#define luaL_openlibs(L)  \
  { (luaL_openlibs)(L); luaL_requiref(L, "T", luaB_opentests, 1); }
#endif



#endif
