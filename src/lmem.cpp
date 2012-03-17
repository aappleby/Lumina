/*
** $Id: lmem.c,v 1.83 2011/11/30 12:42:49 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/


#include <stddef.h>
#include <memory.h>
#include <algorithm>

#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"


/*
Memcontrol l_memcontrol =
  {0L, 0L, 0L, 0L, {0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L}};
  */

Memcontrol l_memcontrol;

Memcontrol::Memcontrol() {
  numblocks = 0;
  total = 0;
  maxmem = 0;
  memlimit = 0;

  memset(objcount, 0, sizeof(objcount));
  memset(objcount2, 0, sizeof(objcount2));

  char *limit = getenv("MEMLIMIT");  /* initialize memory limit */
  l_memcontrol.memlimit = limit ? strtoul(limit, NULL, 10) : ULONG_MAX;
}


/*
** {======================================================================
** Controlled version for realloc.
** =======================================================================
*/

#define MARK		0x55  /* 01010101 (a nice pattern) */

struct Header {
  size_t size;
  int type;
};

/* full memory check */
#define MARKSIZE	16  /* size of marks after each block */

void checkMark(void* blob, int val, int size) {
  uint8_t* buf = (uint8_t*)blob;
  for (int i = 0; i < size; i++) assert(buf[i] == val);
}

void freeblock (Header *block) {
  if (block == NULL) return;

  l_memcontrol.numblocks--;
  l_memcontrol.total -= block->size;
  l_memcontrol.objcount[block->type]--;

  uint8_t* buf = reinterpret_cast<uint8_t*>(block);
  checkMark(buf + sizeof(Header) + block->size, MARK, MARKSIZE);
  memset(buf, -MARK, sizeof(Header) + block->size + MARKSIZE);

  free(block);
}

Header* allocblock (size_t size, int type) {
  uint8_t* buf = (uint8_t*)malloc(sizeof(Header) + size + MARKSIZE);
  if (buf == NULL) return NULL;

  Header *block = reinterpret_cast<Header*>(buf);
  block->size = size;
  block->type = type;
  memset(buf + sizeof(Header), -MARK, size);
  memset(buf + sizeof(Header) + size, MARK, MARKSIZE);

  l_memcontrol.numblocks++;
  l_memcontrol.total += (uint32_t)size;
  l_memcontrol.objcount[type]++;
  l_memcontrol.objcount2[type]++;
  l_memcontrol.maxmem = std::max(l_memcontrol.maxmem, l_memcontrol.total);

  return block;
}


void *debug_realloc (void* blob, size_t oldsize, size_t newsize, int type) {
  if (blob == NULL) {
    if(newsize == 0) return NULL;
    if (l_memcontrol.total+newsize > l_memcontrol.memlimit) return NULL;
    assert(type >= 0);
    assert(type < 256);
    Header* newblock = allocblock(newsize, type);
    return newblock ? newblock + 1 : NULL;
  }

  Header* oldblock = reinterpret_cast<Header*>(blob) - 1;
  assert(oldsize == oldblock->size);

  if (newsize == 0) {
    freeblock(oldblock);
    return NULL;
  }

  if (l_memcontrol.total+newsize-oldsize > l_memcontrol.memlimit) {
    return NULL;
  }

  Header* newblock = allocblock(newsize, oldblock->type);
  if(newblock == NULL) return NULL;

  memcpy(newblock + 1, oldblock + 1, std::min(oldsize,newsize));
  freeblock(oldblock);

  return newblock + 1;
}

#define MINSIZEARRAY	4


void *luaM_growaux_ (lua_State *L, void *block, int& size, size_t size_elems,
                     int limit, const char *what) {
  THREAD_CHECK(L);
  void *newblock;
  int newsize;
  if (size >= limit/2) {  /* cannot double it? */
    if (size >= limit)  /* cannot grow even a little? */
      luaG_runerror("too many %s (limit is %d)", what, limit);
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
  THREAD_CHECK(L);
  luaG_runerror("memory allocation error: block too big");
}

void* default_alloc(void *ptr, size_t osize, size_t nsize, int type) {
#ifdef LUA_DEBUG
  return debug_realloc(ptr,osize,nsize,type);
#else
  (void)osize;
  return realloc(ptr,nsize);
#endif
}


/*
** generic allocation routine.
*/
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  THREAD_CHECK(L);
  void *newblock;
  global_State *g = G(L);
  size_t realosize = (block) ? osize : 0;
  assert((realosize == 0) == (block == NULL));
  newblock = default_alloc(block, osize, nsize, osize);
  if (newblock == NULL && nsize > 0) {
    api_check(nsize > realosize, "realloc cannot fail when shrinking a block");
    if (g->gcrunning) {
      luaC_fullgc(L, 1);  /* try to free some memory... */
      newblock = default_alloc(block, osize, nsize, osize);  /* try again */
    }
    if (newblock == NULL)
      luaD_throw(L, LUA_ERRMEM);
  }
  assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - realosize;

  return newblock;
}

void* luaM_reallocv(lua_State* L, void* block, size_t osize, size_t nsize, size_t esize) {
  THREAD_CHECK(L);
  if((size_t)(nsize+1) > (MAX_SIZET/esize)) {
    luaM_toobig(L);
    return 0;
  }
  
  return luaM_realloc_(L, block, osize*esize, nsize*esize);
}

void * luaM_newobject(lua_State* L, int tag, size_t size) { 
  THREAD_CHECK(L);
  return luaM_realloc_(L, NULL, tag, size);
}

void luaM_freemem(lua_State* L, void * blob, size_t size) {
  THREAD_CHECK(L);
  luaM_realloc_(L, blob, size, 0);
}

void* luaM_alloc(lua_State* L, size_t size) {
  THREAD_CHECK(L);
  return luaM_realloc_(L, NULL, 0, size);
}

void* luaM_allocv(lua_State* L, size_t n, size_t size) {
  THREAD_CHECK(L);
  return luaM_reallocv(L, NULL, 0, n, size);
}
