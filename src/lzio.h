/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#define EOZ	(-1)			/* end of stream */

class Zio {
public:

  void init (LuaThread* L, lua_Reader reader, void* data);

  int getc() {
    if(n-- > 0) {
      return (unsigned char)*p++;
    }
    else {
      return fill();
    }
  }

  size_t read(void* b, size_t n);

  int fill();

  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void* data;			/* additional data */
  LuaThread *L;			/* Lua state (for reader) */
};

#endif
