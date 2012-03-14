/*
** $Id: lmem.h,v 1.38 2011/12/02 13:26:54 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lua.h"

/* memory allocator control variables */
struct Memcontrol {
  Memcontrol();

  size_t numblocks;
  size_t total;
  size_t maxmem;
  size_t memlimit;
  size_t objcount[256];
  size_t objcount2[256];
};

extern Memcontrol l_memcontrol;


void* luaM_reallocv(lua_State* L, void* block, size_t osize, size_t nsize, size_t esize);

void luaM_freemem(lua_State* L, void * blob, size_t size);

void* luaM_alloc(lua_State* L, size_t size);

void* luaM_allocv(lua_State* L, size_t n, size_t size);

void* luaM_newobject(lua_State* L, int tag, size_t size);

l_noret luaM_toobig (lua_State *L);

/* not to be called directly */
void *luaM_growaux_ (lua_State *L, void *block, int& size,
                               size_t size_elem, int limit,
                               const char *what);

void* default_alloc(void *ptr, size_t osize, size_t nsize, int type);

#endif

