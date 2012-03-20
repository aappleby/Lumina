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

  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  Table *metatable;
  LuaVector<TValue> array;
  LuaVector<Node> hashtable;

  Node* getNode(int i) {
    assert(hashtable.size());
    assert(i >= 0);
    assert(i < hashtable.size());
    return &hashtable[i];
  }

  const Node* getNode(int i) const {
    assert(hashtable.size());
    assert(i >= 0);
    assert(i < hashtable.size());
    return &hashtable[i];
  }

  //LuaVector<Node> node;
  int lastfree;
  LuaObject *graylist;
};