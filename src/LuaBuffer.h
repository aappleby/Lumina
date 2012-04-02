#pragma once
#include "LuaVector.h"

struct Mbuffer {
  LuaVector<char> buffer;
  size_t size_;
};

