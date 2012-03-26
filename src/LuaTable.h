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

  //----------
  // Visitor pattern stuff for GC. Traversal returns the 'cost'
  // of visiting the nodes, used for GC heuristics.

  typedef void (*nodeCallback)(TValue* key, TValue* value, void* blob);
  typedef void (*valueCallback)(TValue* v, void* blob);

  int traverseNodes(Table::nodeCallback c, void* blob);
  int traverseArray(Table::valueCallback c, void* blob);
  int traverse(Table::nodeCallback c, void* blob);

  //----------

  Node* nodeAt(uint32_t hash);

  //----------

  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  Table *metatable;
  int lastfree;

  LuaVector<TValue> array;
  LuaVector<Node> hashtable;
};