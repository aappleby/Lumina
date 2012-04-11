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

  bool hasArray() { return !array.empty(); }
  bool hasHash()  { return !hashtable.empty(); }

  // Converts key to/from linear table index.
  int  getTableIndexSize  () const;
  bool keyToTableIndex    (TValue key, int& outIndex);
  bool tableIndexToKeyVal (int index, TValue& outKey, TValue& outValue);

  int getArraySize() const { return (int)array.size(); }
  int getHashSize() const { return (int)hashtable.size(); }

  // Returns the value associated with the key
  // can't turn this to value return until the rest of the code doesn't fetch by pointer...
  const TValue* findValue(TValue key);
  const TValue* findValue(int key);

  Node* getNode(int i) {
    assert(hashtable.size());
    assert(i >= 0);
    assert(i < (int)hashtable.size());
    return &hashtable[i];
  }

  const Node* getNode(int i) const {
    assert(hashtable.size());
    assert(i >= 0);
    assert(i < (int)hashtable.size());
    return &hashtable[i];
  }

  // Main get/set methods, which we'll gradually be transitioning to.
  TValue get(TValue key) const;
  TValue get(int key) const;

  bool   set(TValue key, TValue val);
  bool   set(int key, TValue val);
  
  // This is only used by one test...
  int   findBinIndex(TValue key);

  // This is used in a few places
  void resize(int arrayssize, int hashsize);

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

  Table *metatable;

  LuaVector<TValue> array;
  LuaVector<Node> hashtable;

  int lastfree;

protected:


  // Returns the node matching the key.
  Node* findNode(TValue key);
  Node* findNode(int key);

  // Returns the node where the key would go.
  Node* findBin(TValue key);
  Node* findBin(int key);

  const TValue* findValueInHash(TValue key);
  const TValue* findValueInHash(int key);

  //----------

  Node* getFreeNode();

  void rehash(TValue newkey);

  TValue* newKey(const TValue* key);

  //----------
  // Visitor pattern stuff.

  typedef void (*nodeCallback)(TValue* key, TValue* value, void* blob);
  typedef void (*valueCallback)(TValue* v, void* blob);

  int traverseNodes(Table::nodeCallback c, void* blob);
  int traverseArray(Table::valueCallback c, void* blob);
  int traverse(Table::nodeCallback c, void* blob);
};