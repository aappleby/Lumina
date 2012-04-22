#pragma once
#include "LuaObject.h"
#include "LuaVector.h"

class stringtable;

/*
** Header for string value; string bytes follow the end of this structure
*/
__declspec(align(8)) class TString : public LuaObject {
public:

  ~TString();

  size_t getLen() const { return len_; }

  const char * c_str() const {
    return buf_;
  }

  uint8_t getReserved() const { return reserved_; }
  void setReserved(uint8_t r) { reserved_ = r; }

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

protected:

  TString(stringtable* parent, uint32_t hash, const char* str, int len);
  uint32_t getHash() const { return hash_; }

  friend class stringtable;

  stringtable* parent_;
  char* buf_;
  uint8_t reserved_;
  uint32_t hash_;
  size_t len_;  /* number of characters in string */

};

class stringtable {
public:

  stringtable();
  ~stringtable();

  TString* Create(const char* str);
  TString* Create(const char* str, int len);

  void resize(int newsize);

//private:
  LuaVector<LuaObject*> hash_;
  uint32_t nuse_;
  int size_;
  int sweepCursor_;

protected:

  TString* find(uint32_t hash, const char* str, size_t len);
};
