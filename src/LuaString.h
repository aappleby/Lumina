#pragma once
#include "LuaList.h"
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

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  uint32_t getHash() const { return hash_; }

protected:

  LuaString(uint32_t hash, const char* str, int len);
  
  friend class LuaStringTable;

  char* buf_;
  uint32_t hash_;
  size_t len_;  /* number of characters in string */

};

class LuaStringTable {
public:

  LuaStringTable();
  ~LuaStringTable();

  LuaString* Create(const std::string& str);
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
  LuaString* getStringAt(int index) { return (LuaString*)hash_[index].begin().get(); }

protected:

  LuaVector<LuaList> hash_;
  uint32_t nuse_;
  int sweepCursor_;

  LuaString* find(uint32_t hash, const char* str, size_t len);
};
