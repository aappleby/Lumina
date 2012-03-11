#pragma once
#include "LuaBase.h"

/*
** Header for string value; string bytes follow the end of this structure
*/
__declspec(align(8)) class TString : public LuaBase {
public:

  TString() {}

  const char * c_str() const { return reinterpret_cast<const char *>(this+1); }

  uint32_t getHash() const { return hash; }
  void setHash(uint32_t h) { hash = h; }

  size_t getLen() const { return len; }
  void setLen(size_t l) { len = l; }

  void setText(const char * str) {
    memcpy(this+1, str, len*sizeof(char));
  }

  uint8_t getReserved() const { return reserved; }
  void setReserved(uint8_t r) { reserved = r; }

protected:

  uint8_t reserved;
  uint32_t hash;
  size_t len;  /* number of characters in string */

};
