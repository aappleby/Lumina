#pragma once
#include "LuaObject.h"

/*
** Header for userdata; memory area follows the end of this structure
*/
__declspec(align(8)) class LuaBlob : public LuaObject {
public:

  LuaBlob(size_t len);
  ~LuaBlob();

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  LuaTable* metatable_;
  LuaTable* env_;
  uint8_t* buf_;
  size_t len_;  /* number of bytes */

  // patched in temporarily while we move file IO to a subclass
  FILE *f;  /* stream (NULL for incompletely created streams) */
  LuaCallback closef;  /* to close stream (NULL for closed streams) */
};

