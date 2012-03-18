#pragma once
#include "LuaBase.h"
#include "LuaValue.h"
#include "LuaVector.h"

class Table;

class Node {
public:
  TValue i_val;
  TValue i_key;
  Node *next;  /* for chaining */
};


class Table : public LuaBase {
public:
  Table() {}

  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  uint8_t lsizenode;  /* log2 of size of `node' array */
  Table *metatable;
  //TValue *array;  /* array part */
  LuaVector<TValue> array;
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  LuaBase *gclist;
  int sizearray;  /* size of `array' array */
};