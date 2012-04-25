#pragma once
#include "LuaObject.h"
#include "LuaVector.h"

class LuaStringTable;

/*
** Header for string value; string bytes follow the end of this structure
*/
class LuaString : public LuaObject {
public:

  ~LuaString();

  size_t getLen() const { return len_; }
  const char* c_str() const { return buf_; }

  int getReserved() const { return reserved_; }
  void setReserved(int r) { reserved_ = r; }

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

protected:

  LuaString(uint32_t hash, const char* str, int len);
  uint32_t getHash() const { return hash_; }

  friend class LuaStringTable;

  char* buf_;
  int reserved_;
  uint32_t hash_;
  size_t len_;  /* number of characters in string */

};

class LuaStringTable {
public:

  LuaStringTable();
  ~LuaStringTable();

  LuaString* Create(const char* str);
  LuaString* Create(const char* str, int len);

  void Resize(int newsize);
  void Shrink();
  void Clear();

  bool Sweep(bool generational);
  void RestartSweep();

  // These are used only by ltests.cpp
  int getStringCount() const { return nuse_; }
  int getHashSize() const { return (int)hash_.size(); }
  LuaString* getStringAt(int index) const { return (LuaString*)hash_[index]; }

protected:

  LuaVector<LuaString*> hash_;
  uint32_t nuse_;
  int sweepCursor_;

  LuaString* find(uint32_t hash, const char* str, size_t len);
};
