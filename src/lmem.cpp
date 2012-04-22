/*
** $Id: lmem.c,v 1.83 2011/11/30 12:42:49 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/


#include <stddef.h>
#include <memory.h>
#include <algorithm>
#include <assert.h>

#include "LuaTypes.h"
#include "LuaGlobals.h"

#include "lmem.h"

void luaC_fullgc (int isemergency);

Memcontrol l_memcontrol;

Memcontrol::Memcontrol() {
  numblocks = 0;
  total = 0;
  maxmem = 0;
  memlimit = 0;
  limitDisabled = 0;

  char *limit = getenv("MEMLIMIT");  /* initialize memory limit */
  memlimit = limit ? strtoul(limit, NULL, 10) : ULONG_MAX;
}

bool Memcontrol::free(size_t size) {
  numblocks--;
  total -= size;
  return true;
}

bool Memcontrol::alloc(size_t size) {
  numblocks++;
  total += (uint32_t)size;
  maxmem = std::max(maxmem, total);
  return true;
}

bool Memcontrol::canAlloc(size_t size) {
  return total+size <= memlimit;
}

bool Memcontrol::isOverLimit() {
  return total > memlimit;
}

void Memcontrol::enableLimit() {
  assert(limitDisabled);
  limitDisabled--;
}

void Memcontrol::disableLimit() { 
  //assert(!limitDisabled);
  if(limitDisabled) {
    int b = 1;
    b++;
  }
  limitDisabled++;
}

void Memcontrol::checkLimit() {
  if(limitDisabled || !isOverLimit()) return;

  // Limit in place and we're over it. Try running an emergency garbage
  // collection cycle.
  if (thread_G && thread_G->gcrunning) {
    luaC_fullgc(1);
  }

  // If we're still over, throw the out-of-memory error.
  if(isOverLimit()) {
    handleResult(LUA_ERRMEM);
  }
}

//-----------------------------------------------------------------------------

#define MARK		0x55  /* 01010101 (a nice pattern) */
#define MARKSIZE	16  /* size of marks after each block */

struct Header {
  uint64_t size;
  uint64_t type;
};

void *luaM_alloc_nocheck (size_t size) {
  assert(l_memcontrol.limitDisabled);

  uint8_t* buf = (uint8_t*)malloc(16 + size + MARKSIZE);
  if (buf == NULL) return NULL;

  memset(buf + 16, -MARK, size);
  memset(buf + 16 + size, MARK, MARKSIZE);
  l_memcontrol.alloc(size);

  if(thread_G) thread_G->incTotalBytes((int)size);

  Header *block = reinterpret_cast<Header*>(buf);
  block->size = size;

  return block + 1;
}

void luaM_free(void * blob) {
  if(blob == NULL) return;
  Header* block = reinterpret_cast<Header*>(blob) - 1;
  
  l_memcontrol.free((size_t)block->size);

  if(thread_G) thread_G->incTotalBytes(-(int)block->size);

  uint8_t* buf = reinterpret_cast<uint8_t*>(block);
  uint8_t* mark = buf + 16 + block->size;
  for (int i = 0; i < MARKSIZE; i++) assert(mark[i] == MARK);
  
  memset(buf, -MARK, 16 + (size_t)block->size + MARKSIZE);

  free(block);
}

//-----------------------------------------------------------------------------
