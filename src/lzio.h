/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#include "lmem.h"
#include "LuaBuffer.h"
#include "LuaVector.h"

#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : luaZ_fill(z))

#define luaZ_buffer(buff)	(&(buff)->buffer[0])
#define luaZ_sizebuffer(buff)	((buff)->buffer.size())
#define luaZ_bufflen(buff)	((buff)->size_)

#define luaZ_resetbuffer(buff) ((buff)->size_ = 0)



char *luaZ_openspace (LuaThread *L, Mbuffer *buff, size_t n);
void luaZ_init (LuaThread *L, ZIO *z, lua_Reader reader,
                                        void *data);
size_t luaZ_read (ZIO* z, void* b, size_t n);	/* read next n bytes */



/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void* data;			/* additional data */
  LuaThread *L;			/* Lua state (for reader) */
};


int luaZ_fill (ZIO *z);

#endif
