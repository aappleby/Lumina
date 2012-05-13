/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

//#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : luaZ_fill(z))

void luaZ_init (LuaThread *L, ZIO *z, lua_Reader reader, void *data);
size_t luaZ_read (ZIO* z, void* b, size_t n);	/* read next n bytes */

int luaZ_fill (ZIO *z);

/* --------- Private Part ------------------ */

struct Zio {

  int getc() {
    if(n-- > 0) {
      return (unsigned char)*p++;
    }
    else {
      return luaZ_fill(this);
    }
  }

  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void* data;			/* additional data */
  LuaThread *L;			/* Lua state (for reader) */
};

#endif
