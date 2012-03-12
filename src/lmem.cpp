/*
** $Id: lmem.c,v 1.83 2011/11/30 12:42:49 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/


#include <stddef.h>

#define lmem_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



#define MINSIZEARRAY	4


void *luaM_growaux_ (lua_State *L, void *block, int& size, size_t size_elems,
                     int limit, const char *what) {
  void *newblock;
  int newsize;
  if (size >= limit/2) {  /* cannot double it? */
    if (size >= limit)  /* cannot grow even a little? */
      luaG_runerror(L, "too many %s (limit is %d)", what, limit);
    newsize = limit;  /* still have at least one free place */
  }
  else {
    newsize = size*2;
    if (newsize < MINSIZEARRAY)
      newsize = MINSIZEARRAY;  /* minimum size */
  }
  newblock = luaM_reallocv(L, block, size, newsize, size_elems);
  size = newsize;  /* update only when everything else is OK */
  return newblock;
}


l_noret luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
}

void *l_alloc (void *ptr, size_t osize, size_t nsize) {
  (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}

void *debug_realloc (void *block, size_t osize, size_t nsize);
void *l_alloc (void *ptr, size_t osize, size_t nsize);

void* default_alloc(void *ptr, size_t osize, size_t nsize) {
#ifdef LUA_DEBUG
  return debug_realloc(ptr,osize,nsize);
#else
  return l_alloc(ptr,osize,nsize);
#endif
}


/*
** generic allocation routine.
*/
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  void *newblock;
  global_State *g = G(L);
  size_t realosize = (block) ? osize : 0;
  assert((realosize == 0) == (block == NULL));
  newblock = default_alloc(block, osize, nsize);
  if (newblock == NULL && nsize > 0) {
    api_check(L, nsize > realosize,
                 "realloc cannot fail when shrinking a block");
    if (g->gcrunning) {
      luaC_fullgc(L, 1);  /* try to free some memory... */
      newblock = default_alloc(block, osize, nsize);  /* try again */
    }
    if (newblock == NULL)
      luaD_throw(L, LUA_ERRMEM);
  }
  assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - realosize;

  return newblock;
}

void* luaM_reallocv(lua_State* L, void* block, size_t osize, size_t nsize, size_t esize) {
  if((size_t)(nsize+1) > (MAX_SIZET/esize)) {
    luaM_toobig(L);
    return 0;
  }
  
  return luaM_realloc_(L, block, osize*esize, nsize*esize);
}

void * luaM_newobject(lua_State* L, int tag, size_t size) { 
  return luaM_realloc_(L, NULL, tag, size);
}

void luaM_freemem(lua_State* L, void * blob, size_t size) {
  luaM_realloc_(L, blob, size, 0);
}

void* luaM_alloc(lua_State* L, size_t size) {
  return luaM_realloc_(L, NULL, 0, size);
}

void* luaM_allocv(lua_State* L, size_t n, size_t size) {
  return luaM_reallocv(L, NULL, 0, n, size);
}
