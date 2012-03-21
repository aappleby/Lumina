/*
** $Id: lmem.c,v 1.83 2011/11/30 12:42:49 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/


#include <stddef.h>
#include <memory.h>
#include <algorithm>

#include "LuaGlobals.h"

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

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

#define MARK		0x55  /* 01010101 (a nice pattern) */
#define MARKSIZE	16  /* size of marks after each block */

struct Header {
  size_t size;
  int type;
  int pool;
};

//-----------------------------------------------------------------------------

Header* allocblock (size_t size, int type, int pool) {
  uint8_t* buf = (uint8_t*)malloc(sizeof(Header) + size + MARKSIZE);
  if (buf == NULL) return NULL;

  Header *block = reinterpret_cast<Header*>(buf);
  block->size = size;
  block->type = type;
  block->pool = pool;
  memset(buf + sizeof(Header), -MARK, size);
  memset(buf + sizeof(Header) + size, MARK, MARKSIZE);

  return block;
}

//----------

void freeblock (Header *block) {
  if (block == NULL) return;

  uint8_t* buf = reinterpret_cast<uint8_t*>(block);
  uint8_t* mark = buf + sizeof(Header) + block->size;
  for (int i = 0; i < MARKSIZE; i++) assert(mark[i] == MARK);
  memset(buf, -MARK, sizeof(Header) + block->size + MARKSIZE);

  free(block);
}

//-----------------------------------------------------------------------------

void* default_alloc(size_t size, int type, int pool) {
  if (!l_memcontrol.canAlloc(size)) return NULL;
  assert(type >= 0);
  assert(type < 256);
  Header* newblock = allocblock(size, type, pool);
  if(newblock) l_memcontrol.alloc(size,type);
  return newblock ? newblock + 1 : NULL;
}

void default_free(void * blob) {
  if(blob == NULL) return;
  Header* block = reinterpret_cast<Header*>(blob) - 1;
  l_memcontrol.free(block->size, block->type);
  if(thread_G) thread_G->GCdebt -= block->size;
  freeblock(block);
}

//-----------------------------------------------------------------------------

void *luaM_alloc_ (size_t size, int type, int pool) {
  void* newblock = default_alloc(size, type, pool);
  if (newblock == NULL) {
    if (thread_G && thread_G->gcrunning) {
      luaC_fullgc(1);  /* try to free some memory... */
      newblock = default_alloc(size, type, pool);  /* try again */
    }
    if (newblock == NULL) {
      if(thread_L) luaD_throw(LUA_ERRMEM);
    }
  }

  if(thread_G) thread_G->GCdebt += size;

  return newblock;
}

//-----------------------------------------------------------------------------

/*
** create a new collectable object (with given type and size) and link
** it to '*list'.
*/
LuaObject *luaC_newobj (int type, size_t size, LuaObject **list) {
  LuaObject* o = (LuaObject*)luaM_alloc_(size,type,LAP_OBJECT);
  o->Init(type, list);
  return o;
}

void luaM_delobject(void * blob) {
  if(blob == NULL) return;
  default_free(blob);
}

void* luaM_alloc(size_t size, int pool) {
  return luaM_alloc_(size, 0, pool);
}

void luaM_free(void * blob) {
  if(blob == NULL) return;
  default_free(blob);
}

//-----------------------------------------------------------------------------
