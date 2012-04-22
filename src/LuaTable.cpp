#include "LuaTable.h"

void getTableMode(Table* t, bool& outWeakKey, bool& outWeakVal);
int luaO_ceillog2 (unsigned int x);

/*
** max size of array part is 2^MAXBITS
*/
#define MAXBITS		30
#define MAXASIZE	(1 << MAXBITS)

//-----------------------------------------------------------------------------

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

//-----------------------------------------------------------------------------

Table::Table(int arrayLength, int hashLength) 
: LuaObject(LUA_TTABLE) {
  assert(l_memcontrol.limitDisabled);
  metatable = NULL;
  linkGC(getGlobalGCHead());

  if(arrayLength || hashLength) {
    resize(arrayLength, hashLength);
  }
}

//-----------------------------------------------------------------------------

int Table::getLength() const {
  int start = 30;
  int cursor = 0;
  
  // Exponential search up (starting at 32) until we find a nil,
  for(int j = 5; j < 30; j++) {
    TValue v = get(TValue(1 << j));
    if(v.isNone() || v.isNil()) {
      start = j-1;
      break;
    }
  }

  // then binary search below it to find the end.
  for(int i = start; i >= 0; i--) {
    int step = (1 << i);
    TValue v = get(TValue(cursor+step));
    if(!v.isNone() && !v.isNil()) {
      cursor += step;
    }
  }

  return cursor;
}

//-----------------------------------------------------------------------------

Table::Node* Table::findBin(TValue key) {
  if(hash_.empty()) return NULL;

  uint32_t hash = key.hashValue();
  uint32_t mask = (uint32_t)hash_.size() - 1;

  return &hash_[hash & mask];
}

Table::Node* Table::findBin(int key) {
  if(hash_.empty()) return NULL;

  TValue key2(key);
  uint32_t hash = key2.hashValue();
  uint32_t mask = (uint32_t)hash_.size() - 1;

  return &hash_[hash & mask];
}

//-----------------------------------------------------------------------------

Table::Node* Table::findNode(TValue key) {
  if(hash_.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

Table::Node* Table::findNode(int key) {
  if(hash_.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

//-----------------------------------------------------------------------------
// Linear index <-> key-val conversion, used to (inefficiently) implement
// lua_next.

int Table::getTableIndexSize() const {
  return (int)(array_.size() + hash_.size());
}

bool Table::keyToTableIndex(TValue key, int& outIndex) {
  if(key.isInteger()) {
    int index = key.getInteger() - 1; // lua index -> c index
    if((index >= 0) && (index < (int)array_.size())) {
      outIndex = index;
      return true;
    }
  }

  Node* node = findNode(key);
  if(node == NULL) return false;
  
  outIndex = (int)(node - hash_.begin()) + (int)array_.size();
  return true;
}

bool Table::tableIndexToKeyVal(int index, TValue& outKey, TValue& outVal) {
  if(index < 0) return false;
  if(index < (int)array_.size()) {
    outKey = TValue(index + 1); // c index -> lua index
    outVal = array_[index];
    return true;
  }

  index -= (int)array_.size();
  if(index < (int)hash_.size()) {
    outKey = hash_[index].i_key;
    outVal = hash_[index].i_val;
    return true;
  }

  return false;
}

//-----------------------------------------------------------------------------

TValue Table::get(TValue key) const {
  if(key.isNil()) return TValue::None();

  if(key.isInteger()) {
    // lua index -> c index
    int intkey = key.getInteger();
    int index = intkey - 1;
    if((index >= 0) && (index < (int)array_.size())) {
      return array_[index];
    }

    // Important - if the integer wasn't in the array, we have convert it
    // back into a TValue key in order to catch the negative-zero case.
    key = intkey;
  }

  // Non-integer key, search the hash table.

  if(hash_.empty()) return TValue::None();

  uint32_t hash = key.hashValue();
  uint32_t mask = (uint32_t)hash_.size() - 1;

  const Node* node = &hash_[hash & mask];

  for(; node; node = node->next) {
    if(node->i_key == key) {
      return node->i_val;
    }
  }

  return TValue::None();
}

//-----------------------------------------------------------------------------

Table::Node* Table::getFreeNode() {
  while (lastfree > 0) {
    lastfree--;
    Node* last = &hash_[lastfree];
    if (last->i_key.isNil())
      return last;
  }
  return NULL;
}

//-----------------------------------------------------------------------------

// inserts a new key into a hash table; first, check whether key's main
// position is free. If not, check whether colliding node is in its main
// position or not: if it is not, move colliding node to an empty place and
// put new key in its main position; otherwise (colliding node is in its main
// position), new key goes to an empty position.
//

int Table::set(TValue key, TValue val) {
  assert(l_memcontrol.limitDisabled);
  // Check for nil keys
  if (key.isNil()) {
    assert(false);
    return LUA_ERRKEY;
  }

  // Check for NaN keys
  if (key.isNumber()) {
    double n = key.getNumber();
    if(n != n) {
      assert(false);
      return LUA_ERRKEY;
    }
  }

  // Check for integer key
  if(key.isInteger()) {
    // Lua index -> C index
    int index = key.getInteger() - 1;
    if((index >= 0) && (index < (int)array_.size())) {
      array_[index] = val;
      return LUA_OK;
    }
  }

  // Not an integer key, or integer doesn't fall in the array. Is there
  // already a node in the hash table for it?
  Node* node = findNode(key);
  if(node) {
    node->i_val = val;
    return LUA_OK;
  }
  
  // No node for that key. Can we just put the key in its primary position?
  Node* primary_node = findBin(key);
  if(primary_node && primary_node->i_val.isNil()) {
    primary_node->i_key = key;
    primary_node->i_val = val;
    return LUA_OK;
  }

  // Nope, primary position occupied. Get a free node in the hash_
  Node *new_node = getFreeNode();

  // If there are no free nodes, rehash to make space for the key and repeat.
  if (new_node == NULL) {
    int arraysize, hashsize;
    computeOptimalSizes(key, arraysize, hashsize);
    resize(arraysize, hashsize);
    return set(key,val);
  }

  // Otherwise, move the old contents of the primary node to the new node
  new_node->i_key = primary_node->i_key;
  new_node->i_val = primary_node->i_val;
  new_node->next = primary_node->next;

  // and put our new key-val in the primary node.
  primary_node->i_key = key;
  primary_node->i_val = val;
  primary_node->next = new_node;
  return LUA_OK;
}

//-----------------------------------------------------------------------------

void countKey( TValue key, int* logtable ) {
  if(key.isInteger()) {
    int k = key.getInteger();
    if((0 < k) && (k <= MAXASIZE)) {
      logtable[luaO_ceillog2(k)]++;
    }
  }
}

void Table::computeOptimalSizes(TValue newkey, int& outArraySize, int& outHashSize) {
  int totalKeys = 0;
  int logtable[32];

  memset(logtable,0,32*sizeof(int));

  for(int i = 0; i < (int)array_.size(); i++) {
    if(array_[i].isNil()) continue;
    // C index -> Lua index
    TValue key(i+1);
    countKey(key, logtable);
    totalKeys++;
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    TValue& key = hash_[i].i_key;
    TValue& val = hash_[i].i_val;
    if(val.isNil()) continue;
    countKey(key, logtable);
    totalKeys++;
  }

  countKey(newkey, logtable);
  totalKeys++;

  int bestSize = 0;
  int bestCount = 0;
  int arrayCount = 0;
  for(int i = 0; i < 30; i++) {
    int arraySize = (1 << i);
    arrayCount += logtable[i];
    if(arrayCount > (arraySize/2)) {
      bestSize = arraySize;
      bestCount = arrayCount;
    }
  }

  int hashSize = totalKeys - bestCount;

  // NOTE(aappleby): The old code didn't include padding for the hash table,
  // which meant that if the number of keys was near a power of 2 the table
  // could get rehashed multiple times in succession. We're padding it out
  // by an additional 1/8th here.
  hashSize += hashSize >> 3;

  outArraySize = bestSize;
  outHashSize = hashSize;
}

//-----------------------------------------------------------------------------
// Note - new memory for array & hash _must_ be allocated before we start moving things around,
// otherwise the allocation could trigger a GC pass which would try and traverse this table while
// it's in an invalid state.

// #TODO - Table resize should be effectively atomic...

int Table::resize(int nasize, int nhsize) {
  assert(l_memcontrol.limitDisabled);

  int oldasize = (int)array_.size();
  int oldhsize = (int)hash_.size();

  // Allocate temporary storage for the resize before we modify the table
  LuaVector<Node> temphash;
  LuaVector<TValue> temparray;

  if(nasize) {
    temparray.resize_nocheck(nasize);
    memcpy(temparray.begin(), array_.begin(), std::min(oldasize, nasize) * sizeof(TValue));
  }

  if (nhsize) {
    int lsize = luaO_ceillog2(nhsize);
    nhsize = 1 << lsize;
    temphash.resize_nocheck(nhsize);
  }

  // Memory allocated, swap and reinsert
  temparray.swap(array_);
  temphash.swap(hash_);
  lastfree = (int)hash_.size(); // all positions are free

  // Move array overflow to hash_
  for(int i = (int)array_.size(); i < (int)temparray.size(); i++) {
    if (!temparray[i].isNil()) {
      set(TValue(i+1), temparray[i]);
    }
  }

  // And finally re-insert the saved nodes.
  for (int i = (int)temphash.size() - 1; i >= 0; i--) {
    Node* old = &temphash[i];
    if (!old->i_val.isNil()) {
      set(old->i_key, old->i_val);
    }
  }

  return LUA_OK;
}

//-----------------------------------------------------------------------------

int Table::traverse(Table::nodeCallback c, void* blob) {
  TValue temp;

  for(int i = 0; i < (int)array_.size(); i++) {
    temp = i + 1; // c index -> lua index;
    c(temp,array_[i],blob);
  }
  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];
    c(n.i_key, n.i_val, blob);
  }

  return TRAVCOST + (int)array_.size() + 2 * (int)hash_.size();
}

//-----------------------------------------------------------------------------

void Table::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);

  for(int i = 0; i < (int)array_.size(); i++) {
    if(array_[i].isString()) {
      array_[i].getObject()->setColor(LuaObject::GRAY);
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    if(n.i_key.isString()) n.i_key.getObject()->setColor(LuaObject::GRAY);
    if(n.i_val.isString()) n.i_val.getObject()->setColor(LuaObject::GRAY);
  }
}

//----------

int Table::PropagateGC(GCVisitor& visitor) {
  visitor.MarkObject(metatable);

  bool weakkey = false;
  bool weakval = false;

  getTableMode(this, weakkey, weakval);

  if(!weakkey) {
    if(!weakval) {
      // Strong keys, strong values - use strong table traversal.
      return PropagateGC_Strong(visitor);
    } else {
      // Strong keys, weak values - use weak table traversal.
      return PropagateGC_WeakValues(visitor);
    }
  } else {
    if (!weakval) {
      // Weak keys, strong values - use ephemeron traversal.
      return PropagateGC_Ephemeron(visitor);
    } else {
      // Both keys and values are weak, don't traverse.
      visitor.PushAllWeak(this);
      return TRAVCOST;
    }
  }
}

//----------

int Table::PropagateGC_Strong(GCVisitor& visitor) {
  setColor(BLACK);

  for(int i = 0; i < (int)array_.size(); i++) {
    visitor.MarkValue(array_[i]);
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    if(n.i_val.isNil()) {
      if (n.i_key.isWhite()) {
        n.i_key = TValue::Nil();
      }
    } else {
      visitor.MarkValue(n.i_key);
      visitor.MarkValue(n.i_val);
    }
  }

  return TRAVCOST + (int)array_.size() + 2 * (int)hash_.size();
}

//----------

int Table::PropagateGC_WeakValues(GCVisitor& visitor) {
  bool hasDeadValues = false;

  for(int i = 0; i < (int)array_.size(); i++) {
    if(array_[i].isLiveColor()) hasDeadValues = true;
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    // Sweep dead keys with no values, mark all other
    // keys.
    if(n.i_val.isNil() && n.i_key.isWhite()) {
      n.i_key = TValue::Nil();
    } else {
      visitor.MarkValue(n.i_key);
    }

    if(n.i_val.isLiveColor()) hasDeadValues = true;
  }

  if (hasDeadValues) {
    visitor.PushWeak(this);
  }
  else {
    visitor.PushGrayAgain(this);
  }

  return TRAVCOST + (int)hash_.size();
}

//----------
// weak keys, strong values

int Table::PropagateGC_Ephemeron(GCVisitor& visitor) {
  bool propagate = false;
  bool hasDeadKeys = false;

  for(int i = 0; i < (int)array_.size(); i++) {
    if (array_[i].isWhite()) {
      visitor.MarkValue(array_[i]);
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    // sweep keys for nil values
    if (n.i_val.isNil()) {
      if (n.i_key.isWhite()) {
        n.i_key = TValue::Nil();
      }
      continue;
    }

    if (n.i_key.isLiveColor()) {
      hasDeadKeys = true;
     
      if (n.i_val.isLiveColor()) {
        propagate = true;
      }
    } else {
      // Key is marked, mark the value if it's white.
      if (n.i_val.isWhite()) {
        visitor.MarkValue(n.i_val);
      }
    }
  }

  if (propagate) {
    visitor.PushEphemeron(this);
  } else if (hasDeadKeys) {
    visitor.PushAllWeak(this);
  } else {
    visitor.PushGrayAgain(this);
  }

  return TRAVCOST + (int)array_.size() + (int)hash_.size();
}

//----------

void Table::SweepWhite() {
  for (int i = 0; i < (int)array_.size(); i++) {
    if (array_[i].isLiveColor()) {
      array_[i] = TValue::Nil();
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    if(n.i_key.isLiveColor()) {
      n.i_key = TValue::Nil();
      n.i_val = TValue::Nil();
    }

    if(n.i_val.isLiveColor()) {
      n.i_val = TValue::Nil();
    }
  }
}

//----------

void Table::SweepWhiteKeys() {
  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];
    if(n.i_key.isLiveColor()) {
      n.i_val = TValue::Nil();
      n.i_key = TValue::Nil();
    }
  }
}

//----------

void Table::SweepWhiteVals() {
  for (int i = 0; i < (int)array_.size(); i++) {
    if (array_[i].isLiveColor()) {
      array_[i] = TValue::Nil();
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];
    if(!n.i_val.isLiveColor()) continue;

    // White value. If key was white, key goes away too.
    n.i_val = TValue::nil;
    if (n.i_key.isWhite()) {
      n.i_key = TValue::Nil();
    }
  }
}

//-----------------------------------------------------------------------------
