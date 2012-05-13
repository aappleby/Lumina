/*
** $Id: lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include <string>
#include <vector>

#define EOZ	(-1)			/* end of stream */

class Zio {
public:

  Zio() {}
  ~Zio() {}

  void init (const char* buffer, size_t len);

  // does _not_ advance the read cursor
  int next();
  std::string next(size_t len);

  int getc();
  size_t read(void* b, size_t n);

  void skip(size_t len);

  bool empty() {
    return cursor_ == buffer_.size();
  }

private:

  std::vector<char> buffer_;
  size_t cursor_;
};

#endif
