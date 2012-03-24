#include "LuaTable.h"

uint32_t hash32 (uint32_t a);
uint32_t hash64 (uint32_t a, uint32_t b);

Table::Table() : LuaObject(LUA_TTABLE) {
  metatable = NULL;
  flags = 0xFF;
}

Node* Table::getBin(double key) {
  uint32_t* block = reinterpret_cast<uint32_t*>(&key);
  return nodeAt( hash64(block[0],block[1]) );
}

Node* Table::getBin(void* key) {
  uint32_t* block = reinterpret_cast<uint32_t*>(&key);
  if(sizeof(key) == 8) {
    return nodeAt( hash64(block[0],block[1]) );
  } else {
    return nodeAt( hash32(block[0]) );
  }
}

Node* Table::nodeAt(uint32_t hash) {
  if(hashtable.empty()) return NULL;

  uint32_t mask = (uint32_t)hashtable.size() - 1;
  return &hashtable[hash & mask];
}