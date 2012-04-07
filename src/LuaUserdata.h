#pragma once
#include "LuaObject.h"

/*
** Header for userdata; memory area follows the end of this structure
*/
__declspec(align(8)) class Udata : public LuaObject {
public:

  Udata(uint8_t* buf, size_t len, Table* env);
  ~Udata();

  virtual void VisitGC(GCVisitor& visitor);

  Table* metatable_;
  Table* env_;
  uint8_t* buf_;
  size_t len_;  /* number of bytes */
};

