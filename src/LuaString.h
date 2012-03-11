#pragma once
#include "LuaBase.h"

/*
** Header for string value; string bytes follow the end of this structure
*/
__declspec(align(8)) class TString : public LuaBase {
public:

  const char * c_str() const { return reinterpret_cast<const char *>(this+1); }

protected:
  uint8_t reserved;
  unsigned int hash;
  size_t len;  /* number of characters in string */

};
