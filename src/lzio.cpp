/*
** $Id: lzio.c,v 1.34 2011/07/15 12:35:32 roberto Exp $
** a generic input stream interface
** See Copyright Notice in lua.h
*/

#include "lzio.h"

#include <string.h>

//------------------------------------------------------------------------------

void skipHeader(Zio&) {
  /*
  if(strcmp(s.c_str(), "\xEF\xBB\xBF") == 0) {
    s.erase(0,3);
  }

  if(s.size() && (s[0] == '#')) {
    while(s.size() && (s[0] != '\n')) s.erase(0,1);
    if(strcmp(s.c_str(), LUA_SIGNATURE) != 0) {
      s.insert(s.begin(), '\n');
    }
  }
  */
}

//------------------------------------------------------------------------------

void Zio2::init(LuaThread* L2, lua_Reader reader2, void* data2) {
  L = L2;
  reader = reader2;
  data = data2;
  eof_ = false;
  cursor_ = 0;
}

void Zio2::init(const char* buffer, size_t len) {
  L = NULL;
  reader = NULL;
  data = NULL;
  eof_ = false;
  buffer_.resize(len);
  memcpy(&buffer_[0], buffer, len);
  cursor_ = 0;
}

void Zio2::fill() {
  if(!empty()) return;
  if(eof_) return;

  if(reader == NULL) {
    eof_ = true;
    return;
  }

  size_t len;
  const char* buf = reader(L, data, &len);

  if (len == 0 || buf == NULL) {
    eof_ = true;
  } else {
    buffer_.insert(buffer_.end(),  buf, buf+len);
  }
}

int Zio2::getc() {
  if(empty()) fill();
  if(eof_) return EOZ;

  int result = (unsigned char)buffer_[cursor_];
  cursor_++;
  return result;
}

size_t Zio2::read (void* buf, size_t len) {
  char* out = (char*)buf;
  while (len) {
    if (empty()) fill();
    if (eof_) return len;

    size_t left = buffer_.size() - cursor_;
    size_t m = (len < left) ? len : left;
    memcpy(out, &buffer_[cursor_], m);

    cursor_ += m;
    out += m;
    len -= m;
  }
  return 0;
}

void Zio2::push(char c) {
  buffer_.insert(buffer_.begin() + cursor_, c);
}

//------------------------------------------------------------------------------

Zio3::Zio3(const char* buffer, size_t len)
: buffer_(buffer, len),
  cursor_(0) {
}

int Zio3::operator*() const {
  if(cursor_ == buffer_.size()) return EOZ;
  return (unsigned char)buffer_[cursor_];
}

void Zio3::operator++() {
  if(cursor_ < buffer_.size()) cursor_++;
}

void Zio3::operator += (size_t len) {
  cursor_ += len;
  if(cursor_ > buffer_.size()) cursor_ = buffer_.size();
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

void Zio3::push(char c) {
  buffer_.insert(buffer_.begin() + cursor_, c);
}

//------------------------------------------------------------------------------
