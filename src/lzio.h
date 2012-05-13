/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "LuaTypes.h"
#include <string>

#define EOZ	(-1)			/* end of stream */

typedef const char * (*lua_Reader) (LuaThread *L, void *ud, size_t *sz);

class Zio {
public:

  virtual int getc() = 0;
  virtual size_t read(void* b, size_t n) = 0;
};

class Zio2 : public Zio {
public:

  void init (LuaThread* L, lua_Reader reader, void* data);

  int getc();

  size_t read(void* b, size_t n);

private:

  void fill();

  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void* data;			/* additional data */
  LuaThread *L;			/* Lua state (for reader) */
  bool eof_;
};

class Zio3 : public Zio {
public:

  Zio3(const char* buffer, size_t len);

  virtual int getc();
  virtual size_t read(void* b, size_t n);

private:
  std::string buffer_;
  size_t cursor_;
};

#endif
