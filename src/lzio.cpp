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
  cursor_ = 0;
}

void Zio2::init(const char* buffer, size_t len) {
  L = NULL;
  reader = NULL;
  data = NULL;
  buffer_.resize(len);
  memcpy(&buffer_[0], buffer, len);
  cursor_ = 0;
}

void Zio2::fill() {
  if(eof()) return;

  size_t len;
  const char* buf = reader(L, data, &len);

  if (len == 0 || buf == NULL) {
    reader = NULL;
  } else {
    buffer_.insert(buffer_.end(),  buf, buf+len);
  }
}

int Zio2::next() {
  if(empty()) fill();
  if(eof()) return EOZ;
  return (unsigned char)buffer_[cursor_];
}

int Zio2::getc() {
  int result = next();
  if(result != EOZ) cursor_++;
  return result;
}

size_t Zio2::read (void* buf, size_t len) {
  char* out = (char*)buf;
  while (len) {
    if (empty()) fill();
    if (eof()) return len;

    size_t left = buffer_.size() - cursor_;
    size_t m = (len < left) ? len : left;
    memcpy(out, &buffer_[cursor_], m);

    cursor_ += m;
    out += m;
    len -= m;
  }
  return 0;
}

/*
std::string Zio2::next(size_t len) {
  std::string result;

  while((buffer_.size() - cursor_) < len) {

  }

  while (len) {
    if (empty()) fill();
    if (eof_) return len;

    size_t left = buffer_.size() - cursor_;
    size_t m = (len < left) ? len : left;

    result.insert(result.end(), buffer_.begin() + cursor, buffer_.begin() + cursor + m);

    cursor_ += m;
    len -= m;
  }

  return result;
}
*/

void Zio2::push(char c) {
  buffer_.insert(buffer_.begin() + cursor_, c);
}

//------------------------------------------------------------------------------
