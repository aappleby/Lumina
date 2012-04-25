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
  mem_blocks = 0;
  mem_total = 0;
  mem_max = 0;
  mem_limit = ULONG_MAX;
}

bool Memcontrol::canAlloc(size_t size) {
  return (mem_total + size) <= mem_limit;
}

void Memcontrol::checkLimit() {
  if(mem_total <= mem_limit) return;

  // Limit in place and we're over it. Try running an emergency garbage
  // collection cycle.
  if (thread_G && thread_G->gcrunning) {
    luaC_fullgc(1);
  }

  // If we're still over, throw the out-of-memory error.
  if(mem_total > mem_limit) {
    throwError(LUA_ERRMEM);
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

  uint8_t* buf = (uint8_t*)malloc(16 + size + MARKSIZE);
  assert(buf);

  //memset(buf + 16, -MARK, size);
  //memset(buf + 16 + size, MARK, MARKSIZE);

  l_memcontrol.mem_blocks++;
  l_memcontrol.mem_total += (uint32_t)size;
  l_memcontrol.mem_max = std::max(l_memcontrol.mem_max, l_memcontrol.mem_total);

  if(thread_G) thread_G->incTotalBytes((int)size);

  Header *block = reinterpret_cast<Header*>(buf);
  block->size = size;

  return block + 1;
}

void luaM_free(void * blob) {
  if(blob == NULL) return;
  Header* block = reinterpret_cast<Header*>(blob) - 1;
  
  l_memcontrol.mem_blocks--;
  l_memcontrol.mem_total -= (size_t)block->size;

  if(thread_G) thread_G->incTotalBytes(-(int)block->size);

  //uint8_t* buf = reinterpret_cast<uint8_t*>(block);
  //uint8_t* mark = buf + 16 + block->size;
  //for (int i = 0; i < MARKSIZE; i++) assert(mark[i] == MARK);
  
  free(block);
}

//-----------------------------------------------------------------------------
