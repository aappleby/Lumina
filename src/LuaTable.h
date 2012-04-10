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

  // Returns the node matching the key.
  Node* findNode(TValue key);
  Node* findNode(int key);

  // Returns the node where the key would go.
  Node* findBin(TValue key);
  Node* findBin(int key);

  // This is only used by one test...
  int   findBinIndex(TValue key);

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

  const TValue* findValueInHash(TValue key);
  const TValue* findValueInHash(int key);

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

  TValue get(TValue key) const;
  void   set(TValue key, TValue val);

  //----------
  // Visitor pattern stuff for GC. Traversal returns the 'cost'
  // of visiting the nodes, used for GC heuristics.

  typedef void (*nodeCallback)(TValue* key, TValue* value, void* blob);
  typedef void (*valueCallback)(TValue* v, void* blob);

  int traverseNodes(Table::nodeCallback c, void* blob);
  int traverseArray(Table::valueCallback c, void* blob);
  int traverse(Table::nodeCallback c, void* blob);

  // another piece of the gc visitor traversal stuff
  virtual void VisitGC(GCVisitor& visitor);
  virtual int PropagateGC(GCVisitor& visitor);

  int PropagateGC_Strong(GCVisitor& visitor);
  int PropagateGC_WeakValues(GCVisitor& visitor);
  int PropagateGC_Ephemeron(GCVisitor& visitor);

  void SweepWhite();
  void SweepWhiteKeys();
  void SweepWhiteVals();

  //----------

  Node* nodeAt(uint32_t hash);

  Node* getFreeNode();

  void resize(int arrayssize, int hashsize);

  TValue* newKey(const TValue* key);

  //----------

  Table *metatable;

  LuaVector<TValue> array;
  LuaVector<Node> hashtable;

//protected:
  int lastfree;
};