#include "LuaTable.h"

uint32_t hash64 (uint32_t a, uint32_t b) {
  a ^= a >> 16;
  a *= 0x85ebca6b;
  a ^= a >> 13;
  a *= 0xc2b2ae35;
  a ^= a >> 16;

  a ^= b;

  a ^= a >> 16;
  a *= 0x85ebca6b;
  a ^= a >> 13;
  a *= 0xc2b2ae35;
  a ^= a >> 16;

  return a;
}

Node* Table::findNode(TValue key) {
  if(hashtable.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

int Table::findNodeIndex(TValue key) {
  Node* node = findNode(key);
  return node ? (int)(node - hashtable.begin()) : -1;
}

Node* Table::findBin(TValue key) {
  if(hashtable.empty()) return NULL;

  uint32_t hash = hash64(key.low, key.high);
  uint32_t mask = (uint32_t)hashtable.size() - 1;

  return &hashtable[hash & mask];
}

int Table::findBinIndex(TValue key) {
  Node* node = findBin(key);
  return node ? (int)(node - hashtable.begin()) : -1;
}

const TValue* Table::findValue(TValue key) {
  Node* node = findNode(key);
  return node ? &node->i_val : NULL;
}

Table::Table() : LuaObject(LUA_TTABLE) {
  metatable = NULL;
  flags = 0xFF;
}

Node* Table::nodeAt(uint32_t hash) {
  if(hashtable.empty()) return NULL;

  uint32_t mask = (uint32_t)hashtable.size() - 1;
  return &hashtable[hash & mask];
}