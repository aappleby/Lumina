#pragma once
#include "LuaObject.h"
#include "LuaValue.h"
#include "LuaVector.h"

class Table;

class Node {
public:
  TValue i_val;
  TValue i_key;
  Node *next;  /* for chaining */
};


class Table : public LuaObject {
public:

  Table();

  int getLength() const;

  bool hasArray() { return !array.empty(); }
  bool hasHash()  { return !hashtable.empty(); }

  // Converts key to/from linear table index.
  int  getTableIndexSize  () const;
  bool keyToTableIndex    (TValue key, int& outIndex);
  bool tableIndexToKeyVal (int index, TValue& outKey, TValue& outValue);

  // Would be nice if I could remove these, but nextvar.lua fails
  // if I remove the optimization in OP_SETLIST that uses them.
  int getArraySize() const { return (int)array.size(); }
  int getHashSize() const { return (int)hashtable.size(); }

  // Main get/set methods, which we'll gradually be transitioning to.
  TValue get(TValue key) const;
  bool   set(TValue key, TValue val);
  
  // This is used in a few places
  void resize(int arrayssize, int hashsize);

  //----------
  // Test support, not used in actual VM

  int getArraySize() { return array.size(); }
  int getHashSize()  { return hashtable.size(); }

  void getArrayElement ( int index, TValue& outVal ) {
    outVal = array[index];
  }

  void getHashElement ( int index, TValue& outKey, TValue& outVal ) {
    outKey = hashtable[index].i_key;
    outVal = hashtable[index].i_val;
  }

  //----------
  // Garbage collection support, should probably be split out into a subclass

  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

  int PropagateGC_Strong(GCVisitor& visitor);
  int PropagateGC_WeakValues(GCVisitor& visitor);
  int PropagateGC_Ephemeron(GCVisitor& visitor);

  void SweepWhite();
  void SweepWhiteKeys();
  void SweepWhiteVals();

  //----------
  // Visitor pattern stuff. Used in ltests.cpp

  typedef void (*nodeCallback)(const TValue& key, const TValue& value, void* blob);
  int traverse(Table::nodeCallback c, void* blob);

  //----------

  Table *metatable;

protected:

  LuaVector<TValue> array;
  LuaVector<Node> hashtable;

  int lastfree;

  // Returns the node matching the key.
  Node* findNode(TValue key);
  Node* findNode(int key);

  // Returns the node where the key would go.
  Node* findBin(TValue key);
  Node* findBin(int key);

  //----------

  Node* getFreeNode();

  void computeOptimalSizes(TValue newkey, int& arraysize, int& hashsize);

  TValue* newKey(const TValue* key);

  //----------

  /*
  typedef void (*valueCallback)(TValue* v, void* blob);

  int traverseNodes(Table::nodeCallback c, void* blob);
  int traverseArray(Table::valueCallback c, void* blob);
  */
};