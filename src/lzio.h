/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "LuaTypes.h"
#include <string>
#include <vector>

#define EOZ	(-1)			/* end of stream */

typedef const char * (*lua_Reader) (LuaThread *L, void *ud, size_t *sz);

class Zio {
public:

  virtual int next() = 0;
  virtual int getc() = 0;
  virtual size_t read(void* b, size_t n) = 0;
  virtual void push(char c) = 0;
};

class Zio2 : public Zio {
public:

  void init (LuaThread* L, lua_Reader reader, void* data);
  void init (const char* buffer, size_t len);

  int next();
  int getc();
  size_t read(void* b, size_t n);

  // does _not_ advance the read cursor
  std::string next(size_t len);

  void push(char c);

private:

  void fill();

  bool empty() {
    return cursor_ == buffer_.size();
  }

  bool eof() {
    return (cursor_ == buffer_.size()) && (reader == NULL);
  }

  std::vector<char> buffer_;
  size_t cursor_;

  LuaThread *L;
  lua_Reader reader;
  void* data;
};

#endif
