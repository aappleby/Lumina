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
  Table() {}

  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  int sizenode;
  Table *metatable;
  LuaVector<TValue> array;
  LuaVector<Node> node2_;

  Node* getNode(int i) {
    assert(node2_.size());
    assert(sizenode == node2_.size());
    assert(i >= 0);
    assert(i < sizenode);
    return &node2_[i];
  }

  const Node* getNode(int i) const {
    assert(node2_.size());
    assert(sizenode == node2_.size());
    assert(i >= 0);
    assert(i < sizenode);
    return &node2_[i];
  }

  //LuaVector<Node> node;
  int lastfree;
  LuaObject *gclist;

};