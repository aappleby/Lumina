/*
** $Id: lzio.c,v 1.34 2011/07/15 12:35:32 roberto Exp $
** a generic input stream interface
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#include <string.h>

#define lzio_c

#include "lua.h"

#include "llimits.h"
#include "lmem.h"
#include "lstate.h"
#include "lzio.h"


int Zio::fill() {
  size_t size;
  const char *buff = reader(L, data, &size);
  if (buff == NULL || size == 0)
    return EOZ;
  n = size - 1;  /* discount char being returned */
  p = buff;
  return cast_uchar(*p++);
}


void Zio::init(LuaThread* L2, lua_Reader reader2, void* data2) {
  THREAD_CHECK(L2);
  L = L2;
  reader = reader2;
  data = data2;
  n = 0;
  p = NULL;
}


size_t Zio::read (void *b, size_t n2) {
  while (n2) {
    size_t m;
    if (n == 0) {  /* no bytes in buffer? */
      if (fill() == EOZ)  /* try to read more */
        return n2;  /* no more input; return number of missing bytes */
      else {
        n++;  /* luaZ_fill consumed first byte; put it back */
        p--;
      }
    }
    m = (n2 <= n) ? n2 : n;  /* min. between n and z->n */
    memcpy(b, p, m);
    n -= m;
    p += m;
    b = (char *)b + m;
    n2 -= m;
  }
  return 0;
}
