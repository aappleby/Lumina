/*
** $Id: lzio.c,v 1.34 2011/07/15 12:35:32 roberto Exp $
** a generic input stream interface
** See Copyright Notice in lua.h
*/

#include "lzio.h"

#include <string.h>

//------------------------------------------------------------------------------

void skipHeader(Zio& z) {
  // Skip the byte order mark if present.
  if(z.next(3) == "\xEF\xBB\xBF") {
    z.skip(3);
  }

  // If the first character in the stream is a '#', skip everything until the
  // first newline.
  if(z.next() == '#') {
    while((z.next() != '\n') && !z.eof()) {
      z.skip(1);
    }
    // If the data after the newline is the binary Lua signature, skip the
    // newline too.
    if(z.next(5) == "\n\033Lua") {
      z.skip(1);
    }
  }
}

//------------------------------------------------------------------------------

Zio2::~Zio2() {
  if(file_ && (file_ != stdin)) {
    fclose(file_);
  }
}

void Zio2::open(const char* filename) {
  if(strcmp(filename, "stdin") == 0) {
    file_ = stdin;
  }
  else {
    file_ = fopen(filename, "rb");
  }
  if(file_ == NULL) {
    error_ = true;
  }
  thread_ = NULL;
  reader = NULL;
  data = NULL;
  cursor_ = 0;

  skipHeader(*this);
}

void Zio2::init(LuaThread* L2, lua_Reader reader2, void* data2) {
  file_ = NULL;
  thread_ = L2;
  reader = reader2;
  data = data2;
  cursor_ = 0;
}

void Zio2::init(const char* buffer, size_t len) {
  file_ = NULL;
  thread_ = NULL;
  reader = NULL;
  data = NULL;
  buffer_.resize(len);
  memcpy(&buffer_[0], buffer, len);
  cursor_ = 0;
}

void Zio2::init2(LuaThread* L2, lua_Reader reader2, void* data2) {
  init(L2, reader2, data2);
  skipHeader(*this);
}

void Zio2::fill() {
  if(eof()) return;

  if(reader) {
    size_t len;
    const char* buf = reader(thread_, data, &len);

    if (len == 0 || buf == NULL) {
      reader = NULL;
    }
    else {
      buffer_.insert(buffer_.end(),  buf, buf+len);
    }
  }
  else if(file_) {
    char temp[256];
    size_t read = fread(temp, 1, 256, file_);
    if(read == 0) {
      fclose(file_);
      file_ = NULL;
    }
    else {
      buffer_.insert(buffer_.end(), temp, temp + read);
    }
  }
}

int Zio2::next() {
  if(empty()) fill();
  if(eof()) return EOZ;
  return (unsigned char)buffer_[cursor_];
}

int Zio2::getc() {
  int result = next();
  if(!eof()) cursor_++;
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

std::string Zio2::next(size_t len) {

  while((buffer_.size() - cursor_) < len) {
    fill();
    if((reader == NULL) && (file_ == NULL)) break;
   }

  size_t left = buffer_.size() - cursor_;
  size_t m = (len < left) ? len : left;

  return std::string(buffer_.begin() + cursor_,
                     buffer_.begin() + cursor_ + m);
}

void Zio2::push(char c) {
  buffer_.insert(buffer_.begin() + cursor_, c);
}

void Zio2::skip(size_t len) {
  for(size_t i = 0; i < len; i++) {
    getc();
  }
}

//------------------------------------------------------------------------------
