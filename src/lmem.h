/*
** $Id: lmem.h,v 1.38 2011/12/02 13:26:54 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h

//#include <stddef.h>
#include <assert.h>

/* memory allocator control variables */
struct Memcontrol {
  Memcontrol();

  bool canAlloc(size_t size);

  // THROWS AN EXCEPTION if the memory limit has been exceeded.
  void checkLimit();

  size_t mem_blocks;
  size_t mem_total;
  size_t mem_max;
  size_t mem_limit;
};

extern Memcontrol l_memcontrol;

void* luaM_alloc_nocheck(size_t size);

void  luaM_free(void * blob);

#endif

