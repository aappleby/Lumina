#pragma once

#include "LuaObject.h"

#include <string>

class LuaBlob : public LuaObject {
public:

  LuaBlob(size_t len);
  ~LuaBlob();

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  LuaTable* metatable_;
  LuaTable* env_;
  uint8_t* buf_;
  size_t len_;  /* number of bytes */

  std::string usertype_;
};

