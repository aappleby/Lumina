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
  int   findNodeIndex(TValue key);

  // Returns the node where the key would go.
  Node* findBin(TValue key);
  int   findBinIndex(TValue key);

  //int   findTableIndex(TValue key);

  // Returns the value associated with the key
  const TValue* findValue(TValue key);

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

  Node* nodeAt(uint32_t hash);

  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  Table *metatable;
  int lastfree;
  LuaObject *graylist;

  LuaVector<TValue> array;
  LuaVector<Node> hashtable;
};