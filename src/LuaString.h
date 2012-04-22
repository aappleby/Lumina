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

  TString(uint32_t hash, const char* str, int len);
  uint32_t getHash() const { return hash_; }

  friend class stringtable;

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

  int getStringCount() const { return nuse_; }
  int getHashSize() const { return (int)hash_.size(); }

  // Used only by ltests.cpp
  TString* getStringAt(int index) const {
    return (TString*)hash_[index];
  }

  void resize(int newsize);

  bool Sweep(bool generational);

  void Shrink() {
    ScopedMemChecker c;
    if (nuse_ < (uint32_t)(hash_.size() / 2)) resize(hash_.size() / 2);
  }

  void RestartSweep() {
    sweepCursor_ = 0;
  }

  void Clear();

protected:

  LuaVector<TString*> hash_;
  uint32_t nuse_;
  int sweepCursor_;

  TString* find(uint32_t hash, const char* str, size_t len);
};
