#pragma once
#include "LuaVector.h"

struct Mbuffer {
  Mbuffer() {
    size_ = 0;
  }

  ~Mbuffer() {
  }

  LuaVector<char> buffer;
  size_t size_;
};

