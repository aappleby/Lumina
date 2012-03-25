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

int Table::getTableIndexSize() const {
  return (int)(array.size() + hashtable.size());
}

bool Table::keyToTableIndex(TValue key, int& outIndex) {
  if(key.isInteger()) {
    int index = key.getInteger() - 1; // lua index -> c index
    if((index >= 0) && (index < (int)array.size())) {
      outIndex = index;
      return true;
    }
  }

  Node* node = findNode(key);
  if(node == NULL) return false;
  
  outIndex = (node - hashtable.begin()) + array.size();
  return true;
}

bool Table::tableIndexToKeyVal(int index, TValue& outKey, TValue& outVal) {
  if(index < 0) return false;
  if(index < (int)array.size()) {
    outKey = TValue(index + 1); // c index -> lua index
    outVal = array[index];
    return true;
  }

  index -= array.size();
  if(index < (int)hashtable.size()) {
    outKey = hashtable[index].i_key;
    outVal = hashtable[index].i_val;
    return true;
  }

  return false;
}

const TValue* Table::findValueInHash(TValue key) {
  Node* node = findNode(key);
  return node ? &node->i_val : NULL;
}

const TValue* Table::findValue(int key) {
  int index = key - 1; // lua index -> c index
  if((index >= 0) && (index < (int)array.size())) {
    return &array[index];
  }

  return findValueInHash(TValue(key));
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