#pragma once
#include "LuaObject.h"
#include "LuaValue.h"
#include "LuaVector.h"

class LuaTable;

class LuaTable : public LuaObject {
public:

  LuaTable(int arrayLength = 0, int hashLength = 0);

  int getLength() const;

  bool hasArray() { return !array_.empty(); }
  bool hasHash()  { return !hash_.empty(); }

  // Converts key to/from linear table index.
  int  getTableIndexSize  () const;
  bool keyToTableIndex    (LuaValue key, int& outIndex);
  bool tableIndexToKeyVal (int index, LuaValue& outKey, LuaValue& outValue);

  // Would be nice if I could remove these, but nextvar.lua fails
  // if I remove the optimization in OP_SETLIST that uses them.
  int getArraySize() const { return (int)array_.size(); }
  int getHashSize() const { return (int)hash_.size(); }

  // Main get/set methods, which we'll gradually be transitioning to.
  LuaValue get(LuaValue key) const;
  void     set(LuaValue key, LuaValue val);

  // This creates dependencies, but it's used everywhere.
  LuaValue get(const char* key);
  void     set(const char* key, LuaValue val);
  void     set(const char* key, const char* val);

  // Reverse lookup, O(N).
  LuaValue findKey(LuaValue val);
  LuaValue findKeyString(LuaValue val);
  
  // This is used in a few places
  int resize(int arrayssize, int hashsize);

  //----------
  // Test support, not used in actual VM

  int getArraySize() { return (int)array_.size(); }
  int getHashSize()  { return (int)hash_.size(); }

  void getArrayElement ( int index, LuaValue& outVal ) {
    outVal = array_[index];
  }

  void getHashElement ( int index, LuaValue& outKey, LuaValue& outVal ) {
    outKey = hash_[index].i_key;
    outVal = hash_[index].i_val;
  }

  //----------
  // Garbage collection support, should probably be split out into a subclass

  virtual void VisitGC(LuaGCVisitor& visitor);
  virtual int PropagateGC(LuaGCVisitor& visitor);

  int PropagateGC_Strong(LuaGCVisitor& visitor);
  int PropagateGC_WeakValues(LuaGCVisitor& visitor);
  int PropagateGC_Ephemeron(LuaGCVisitor& visitor);

  void SweepWhite();
  void SweepWhiteKeys();
  void SweepWhiteVals();

  //----------
  // Visitor pattern stuff. Used in ltests.cpp

  typedef void (*nodeCallback)(const LuaValue& key, const LuaValue& value, void* blob);
  int traverse(LuaTable::nodeCallback c, void* blob);

  //----------

  LuaTable *metatable;

protected:

  class Node {
  public:
    LuaValue i_val;
    LuaValue i_key;
    Node *next;  /* for chaining */
  };

  LuaVector<LuaValue> array_;
  LuaVector<Node> hash_;

  int lastfree;

  // Returns the node matching the key.
  Node* findNode(LuaValue key);
  Node* findNode(int key);

  // Returns the node where the key would go.
  Node* findBin(LuaValue key);
  Node* findBin(int key);

  //----------

  Node* getFreeNode();

  void computeOptimalSizes(LuaValue newkey, int& arraysize, int& hashsize);

  LuaValue* newKey(const LuaValue* key);

  //----------

  /*
  typedef void (*valueCallback)(LuaValue* v, void* blob);

  int traverseNodes(LuaTable::nodeCallback c, void* blob);
  int traverseArray(LuaTable::valueCallback c, void* blob);
  */
};