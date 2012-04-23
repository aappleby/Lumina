#pragma once
#include "LuaObject.h"
#include "LuaVector.h"

class stringtable;

/*
** Header for string value; string bytes follow the end of this structure
*/
class TString : public LuaObject {
public:

  ~TString();

  size_t getLen() const { return len_; }
  const char* c_str() const { return buf_; }

  int getReserved() const { return reserved_; }
  void setReserved(int r) { reserved_ = r; }

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

protected:

  TString(uint32_t hash, const char* str, int len);
  uint32_t getHash() const { return hash_; }

  friend class stringtable;

  char* buf_;
  int reserved_;
  uint32_t hash_;
  size_t len_;  /* number of characters in string */

};

class stringtable {
public:

  stringtable();
  ~stringtable();

  TString* Create(const char* str);
  TString* Create(const char* str, int len);

  void Resize(int newsize);
  void Shrink();
  void Clear();

  bool Sweep(bool generational);
  void RestartSweep();

  // These are used only by ltests.cpp
  int getStringCount() const { return nuse_; }
  int getHashSize() const { return (int)hash_.size(); }
  TString* getStringAt(int index) const { return (TString*)hash_[index]; }

protected:

  LuaVector<TString*> hash_;
  uint32_t nuse_;
  int sweepCursor_;

  TString* find(uint32_t hash, const char* str, size_t len);
};
