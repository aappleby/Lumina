/*
** $Id: lmem.h,v 1.38 2011/12/02 13:26:54 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h

//#include <stddef.h>

/* memory allocator control variables */
struct Memcontrol {
  Memcontrol();

  bool alloc(size_t size);
  bool free(size_t size);

  bool isOverLimit();
  bool canAlloc(size_t size);

  bool newObject(int type);
  bool delObject(int type);

  void enableLimit();
  void disableLimit();

  // calls to enable/disble limit can be nested.
  int limitDisabled;

  size_t numblocks;
  size_t total;
  size_t maxmem;
  size_t memlimit;
};

extern Memcontrol l_memcontrol;

void* luaM_alloc_nothrow(size_t size);
void* luaM_alloc(size_t size);
void  luaM_free(void * blob);

#endif

