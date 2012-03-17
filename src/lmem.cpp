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

#define MINSIZEARRAY	4


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
  limitEnabled = true;

  memset(objcount, 0, sizeof(objcount));
  memset(objcount2, 0, sizeof(objcount2));

  char *limit = getenv("MEMLIMIT");  /* initialize memory limit */
  memlimit = limit ? strtoul(limit, NULL, 10) : ULONG_MAX;
}

bool Memcontrol::free(size_t size, int type) {
  numblocks--;
  total -= size;
  objcount[type]--;
  return true;
}

bool Memcontrol::alloc(size_t size, int type) {
  numblocks++;
  total += (uint32_t)size;
  objcount[type]++;
  objcount2[type]++;
  maxmem = std::max(maxmem, total);
  return true;
}

bool Memcontrol::canAlloc(size_t size) {
  return !limitEnabled || (total+size <= memlimit);
}

void Memcontrol::enableLimit() {
  assert(!limitEnabled);
  limitEnabled = true;
}

void Memcontrol::disableLimit() { 
  assert(limitEnabled);
  limitEnabled = false;
}

/*
** {======================================================================
** Controlled version for realloc.
** =======================================================================
*/

#define MARK		0x55  /* 01010101 (a nice pattern) */
#define MARKSIZE	16  /* size of marks after each block */

struct Header {
  size_t size;
  int type;
};

//-----------------------------------------------------------------------------

Header* allocblock (size_t size, int type) {
  uint8_t* buf = (uint8_t*)malloc(sizeof(Header) + size + MARKSIZE);
  if (buf == NULL) return NULL;

  Header *block = reinterpret_cast<Header*>(buf);
  block->size = size;
  block->type = type;
  memset(buf + sizeof(Header), -MARK, size);
  memset(buf + sizeof(Header) + size, MARK, MARKSIZE);

  l_memcontrol.alloc(size,type);

  return block;
}

//----------

void freeblock (Header *block) {
  if (block == NULL) return;

  l_memcontrol.free(block->size, block->type);

  uint8_t* buf = reinterpret_cast<uint8_t*>(block);
  uint8_t* mark = buf + sizeof(Header) + block->size;
  for (int i = 0; i < MARKSIZE; i++) assert(mark[i] == MARK);
  memset(buf, -MARK, sizeof(Header) + block->size + MARKSIZE);

  free(block);
}

//-----------------------------------------------------------------------------

void* default_alloc(size_t size, int type) {
  if(size == 0) return NULL;
  if (!l_memcontrol.canAlloc(size)) return NULL;
  assert(type >= 0);
  assert(type < 256);

  Header* newblock = allocblock(size, type);
  return newblock ? newblock + 1 : NULL;
}

void default_free(void * blob, size_t size, int type) {
  Header* oldblock = reinterpret_cast<Header*>(blob) - 1;
  assert(oldblock->size == size);
  assert(oldblock->type == type);
  freeblock(oldblock);
}

void* default_realloc (void* blob, size_t oldsize, size_t newsize, int type) {
  if (blob == NULL) {
    return default_alloc(newsize,type);
  }

  Header* oldblock = reinterpret_cast<Header*>(blob) - 1;
  assert(oldsize == oldblock->size);

  if (newsize == 0) {
    freeblock(oldblock);
    return NULL;
  }

  if (!l_memcontrol.canAlloc(newsize)) {
    return NULL;
  }

  Header* newblock = allocblock(newsize, oldblock->type);
  if(newblock == NULL) return NULL;

  memcpy(newblock + 1, oldblock + 1, std::min(oldsize,newsize));
  freeblock(oldblock);

  return newblock + 1;
}

//-----------------------------------------------------------------------------

void *luaM_alloc_ (size_t size, int type) {
  if(size == 0) return NULL;

  void* newblock = default_realloc(NULL, 0, size, type);
  if (newblock == NULL) {
    if (thread_G->gcrunning) {
      luaC_fullgc(1);  /* try to free some memory... */
      newblock = default_realloc(NULL, 0, size, type);  /* try again */
    }
    if (newblock == NULL)
      luaD_throw(LUA_ERRMEM);
  }

  thread_G->GCdebt += size;

  return newblock;
}

void luaM_free_ (void *block, size_t size, int type) {
  if(block == NULL) return;
  default_free(block, size, type);
  thread_G->GCdebt -= size;
}

void *luaM_realloc_ (void *block, size_t osize, size_t nsize, int type) {
  void *newblock;
  global_State *g = thread_G;
  size_t realosize = (block) ? osize : 0;
  assert((realosize == 0) == (block == NULL));
  newblock = default_realloc(block, osize, nsize, type);
  if (newblock == NULL && nsize > 0) {
    //api_check(nsize > realosize, "realloc cannot fail when shrinking a block");
    if (g->gcrunning) {
      luaC_fullgc(1);  /* try to free some memory... */
      newblock = default_realloc(block, osize, nsize, type);  /* try again */
    }
    if (newblock == NULL)
      luaD_throw(LUA_ERRMEM);
  }
  assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - realosize;

  return newblock;
}

//-----------------------------------------------------------------------------

void* luaM_reallocv(void* block, size_t osize, size_t nsize, size_t esize) {
  assert(block);
  assert(osize);
  assert(nsize);
  return luaM_realloc_(block, osize*esize, nsize*esize, 0);
}

void * luaM_newobject(int tag, size_t size) { 
  return luaM_alloc_(size, tag);
}

void luaM_delobject(void * blob, size_t size, int type) {
//  assert(blob);
//  assert(size);
  assert(type);
  luaM_free_(blob, size, type);
}

void luaM_free(void * blob, size_t size, int type) {
//  assert(blob);
//  assert(size);
  assert(type == 0);
  luaM_free_(blob, size, type);
}

void* luaM_alloc(size_t size) {
  //assert(size);
  return luaM_realloc_(NULL, 0, size, 0);
}

void* luaM_allocv(size_t n, size_t size) {
  //assert(n);
  //assert(size);
  return luaM_alloc_(n*size, 0);
}

//-----------------------------------------------------------------------------

void *luaM_growaux_ (void *block, int& size, size_t size_elems, int limit, const char *what) {
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
  if(block) {
    newblock = luaM_reallocv(block, size, newsize, size_elems);
  } else {
    newblock = luaM_alloc(newsize * size_elems);
  }
  size = newsize;  /* update only when everything else is OK */
  return newblock;
}

