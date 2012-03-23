#pragma once
#include "LuaObject.h"
#include "LuaVector.h"

/*
** Header for string value; string bytes follow the end of this structure
*/
__declspec(align(8)) class TString : public LuaObject {
public:

  TString() : LuaObject(LUA_TSTRING) {
    buf_ = NULL;
    reserved_ = 0;
    hash_ = 0;
    len_ = 0;
  }

  ~TString() {
    luaM_free(buf_);
    buf_ = NULL;
    len_ = NULL;
  }

  size_t getLen() const { return len_; }
  void setLen(size_t len) { len_ = len; }

  void setBuf(char* buf) {
    buf_ = buf;
  }

  const char * c_str() const {
    return buf_;
  }

  void setText(const char * str, size_t len) {
    len_ = len;
    memcpy(buf_, str, len*sizeof(char));
    buf_[len_] = '\0'; // terminating null
  }

  uint32_t getHash() const { return hash_; }
  void setHash(uint32_t hash) { hash_ = hash; }

  uint8_t getReserved() const { return reserved_; }
  void setReserved(uint8_t r) { reserved_ = r; }

protected:

  char* buf_;
  uint8_t reserved_;
  uint32_t hash_;
  size_t len_;  /* number of characters in string */

};

class stringtable {
public:
  //LuaObject **hash;
  LuaVector<LuaObject*> hash;
  uint32_t nuse;  /* number of elements */
  int size;
};
