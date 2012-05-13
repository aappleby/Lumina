/*
** $Id: lzio.c,v 1.34 2011/07/15 12:35:32 roberto Exp $
** a generic input stream interface
** See Copyright Notice in lua.h
*/

#include "lzio.h"

#include <string.h>

void Zio2::init(LuaThread* L2, lua_Reader reader2, void* data2) {
  L = L2;
  reader = reader2;
  data = data2;
  n = 0;
  p = NULL;
  eof_ = false;
}

void Zio2::fill() {
  if(eof_) return;
  p = reader(L, data, &n);
  if (p == NULL || n == 0) {
    eof_ = true;
  }
}

int Zio2::getc() {
  if(n <= 0) fill();
  if(eof_) return EOZ;

  n--;
  return (unsigned char)*p++;
}

size_t Zio2::read (void* buf, size_t len) {
  char* cursor = (char*)buf;
  while (len) {
    if (n == 0) fill();
    if (eof_) return len;
    size_t m = (len < n) ? len : n;  /* min. between n and z->n */
    memcpy(cursor, p, m);

    p += m;
    n -= m;

    cursor += m;
    len -= m;
  }
  return 0;
}

//------------------------------------------------------------------------------

Zio3::Zio3(const char* buffer, size_t len)
: buffer_(buffer, len),
  cursor_(0) {
}

int Zio3::getc() {
  if(cursor_ == buffer_.size()) {
    return EOZ;
  }
  else {
    return (unsigned char)buffer_[cursor_++];
  }
}

size_t Zio3::read(void* buf, size_t len) {
  size_t left = buffer_.size() - cursor_;
  if(left > len) {
    memcpy(buf, &buffer_.c_str()[cursor_], len);
    cursor_ += len;
    return 0;
  }
  else {
    memcpy(buf, &buffer_.c_str()[cursor_], left);
    cursor_ += left;
    return len - left;
  }
}