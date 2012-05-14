/*
** $Id: lzio.c,v 1.34 2011/07/15 12:35:32 roberto Exp $
** a generic input stream interface
** See Copyright Notice in lua.h
*/

#include "lzio.h"

#include <string.h>

//------------------------------------------------------------------------------

void Zio::init(const char* buffer, size_t len) {
  buffer_.resize(len);
  if(len) {
    memcpy(&buffer_[0], buffer, len);
  }
  cursor_ = 0;
}

int Zio::next() {
  if(empty()) return EOZ;
  return (unsigned char)buffer_[cursor_];
}

int Zio::getc() {
  int result = next();
  if(!empty()) cursor_++;
  return result;
}

size_t Zio::read (void* buf, size_t len) {
  char* out = (char*)buf;
  while (len) {
    if (empty()) return len;

    size_t left = buffer_.size() - cursor_;
    size_t m = (len < left) ? len : left;
    memcpy(out, &buffer_[cursor_], m);

    cursor_ += m;
    out += m;
    len -= m;
  }
  return 0;
}

std::string Zio::next(size_t len) {

  size_t left = buffer_.size() - cursor_;
  size_t m = (len < left) ? len : left;

  return std::string(buffer_.begin() + cursor_,
                     buffer_.begin() + cursor_ + m);
}

void Zio::skip(size_t len) {
  cursor_ += len;
  if(cursor_ > buffer_.size()) cursor_ = buffer_.size();
}

//------------------------------------------------------------------------------
