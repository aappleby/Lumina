#include "LuaTable.h"

#include "LuaCollector.h"
#include "LuaGlobals.h"
#include "LuaString.h"

/*
** max size of array part is 2^MAXBITS
*/
#define MAXBITS		30
#define MAXASIZE	(1 << MAXBITS)

int luaO_ceillog2 (unsigned int x) {
  static const uint8_t log_2[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}

//-----------------------------------------------------------------------------

LuaTable::LuaTable(int arrayLength, int hashLength) 
: LuaObject(LUA_TTABLE),
  lastfree(-1) {
  metatable = NULL;
  linkGC(getGlobalGCList());

  if(arrayLength || hashLength) {
    resize(arrayLength, hashLength);
  }
}

//-----------------------------------------------------------------------------

int LuaTable::getLength() {
  int start = 30;
  int cursor = 0;
  
  // Exponential search up (starting at 32) until we find a nil,
  for(int j = 5; j < 30; j++) {
    LuaValue v = get(LuaValue(1 << j));
    if(v.isNone() || v.isNil()) {
      start = j-1;
      break;
    }
  }

  // then binary search below it to find the end.
  for(int i = start; i >= 0; i--) {
    int step = (1 << i);
    LuaValue v = get(LuaValue(cursor+step));
    if(!v.isNone() && !v.isNil()) {
      cursor += step;
    }
  }

  return cursor;
}

//-----------------------------------------------------------------------------

LuaTable::Node* LuaTable::findBin(LuaValue key) {
  if(hash_.empty()) return NULL;

  uint32_t hash = key.hashValue();
  uint32_t mask = (uint32_t)hash_.size() - 1;

  return &hash_[hash & mask];
}

LuaTable::Node* LuaTable::findBin(int key) {
  if(hash_.empty()) return NULL;

  LuaValue key2(key);
  uint32_t hash = key2.hashValue();
  uint32_t mask = (uint32_t)hash_.size() - 1;

  return &hash_[hash & mask];
}

//-----------------------------------------------------------------------------

LuaTable::Node* LuaTable::findNode(LuaValue key) {
  if(hash_.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

LuaTable::Node* LuaTable::findNode(int key) {
  if(hash_.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

//-----------------------------------------------------------------------------
// Linear index <-> key-val conversion, used to (inefficiently) implement
// lua_next.

int LuaTable::getTableIndexSize() const {
  return (int)(array_.size() + hash_.size());
}

bool LuaTable::keyToTableIndex(LuaValue key, int& outIndex) {
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

bool LuaTable::tableIndexToKeyVal(int index, LuaValue& outKey, LuaValue& outVal) {
  if(index < 0) return false;
  if(index < (int)array_.size()) {
    outKey = LuaValue(index + 1); // c index -> lua index
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

LuaValue LuaTable::get(LuaValue key) {
  if(key.isNil()) return LuaValue::None();

  if(key.isInteger()) {
    // lua index -> c index
    int intkey = key.getInteger();
    int index = intkey - 1;
    if((index >= 0) && (index < (int)array_.size())) {
      return array_[index];
    }

    // Important - if the integer wasn't in the array, we have convert it
    // back into a LuaValue key in order to catch the negative-zero case.
    key = intkey;
  }

  // Non-integer key, search the hash table.

  if(hash_.empty()) return LuaValue::None();

  Node* node = findBin(key);

  for(; node; node = node->next) {
    if(node->i_key == key) {
      return node->i_val;
    }
  }

  return LuaValue::None();
}

//-----------------------------------------------------------------------------

LuaTable::Node* LuaTable::getFreeNode() {
  while (lastfree > 0) {
    lastfree--;
    Node* last = &hash_[lastfree];
    if (last->i_key.isNil())
      return last;
  }
  return NULL;
}

//-----------------------------------------------------------------------------
// Adds a key-value pair to the table. Trying to use Nil or None as a key is
// an error.

void LuaTable::set(LuaValue key, LuaValue val) {
  // Check for nil keys
  if (key.isNil()) {
    assert(false);
  }

  // Check for NaN keys
  if (key.isNumber()) {
    double n = key.getNumber();
    if(n != n) {
      assert(false);
    }
  }

  // Check for integer key
  if(key.isInteger()) {
    // Lua index -> C index
    int index = key.getInteger() - 1;
    if((index >= 0) && (index < (int)array_.size())) {
      array_[index] = val;
      return;
    }
  }

  // Not an integer key, or integer doesn't fall in the array. Is there
  // already a node in the hash table for it?
  Node* node = findNode(key);
  if(node) {
    node->i_val = val;
    return;
  }
  
  // No node for that key. Can we just put the key in its primary position?
  Node* primary_node = findBin(key);
  if(primary_node && primary_node->i_val.isNil()) {
    primary_node->i_key = key;
    primary_node->i_val = val;
    return;
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
}

//-----------------------------------------------------------------------------

LuaValue LuaTable::get(const char* keystring) {
  LuaString* key = thread_G->strings_->Create(keystring);

  return get(LuaValue(key));
}

void LuaTable::set(const char* keystring, LuaValue val) {
  LuaString* key = thread_G->strings_->Create(keystring);

  set(LuaValue(key), val);
}

void LuaTable::set(const char* keystring, const char* valstring) {
  if(keystring && valstring) {
    LuaString* key = thread_G->strings_->Create(keystring);
    LuaString* val = thread_G->strings_->Create(valstring);
    set(LuaValue(key), LuaValue(val));
  }
}

//-----------------------------------------------------------------------------

LuaValue LuaTable::findKey( LuaValue val ) {
  for(int i = 0; i < (int)array_.size(); i++) {
    // C index -> Lua index
    if(array_[i] == val) return LuaValue(i+1);
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    if(hash_[i].i_val == val) return hash_[i].i_key;
  }

  return LuaValue::None();
}

LuaValue LuaTable::findKeyString( LuaValue val ) {
  for(int i = 0; i < (int)hash_.size(); i++) {
    if(!hash_[i].i_key.isString()) continue;
    if(hash_[i].i_val == val) return hash_[i].i_key;
  }

  return LuaValue::None();
}

//-----------------------------------------------------------------------------

void countKey(LuaValue key, int* logtable) {
  if(key.isInteger()) {
    int k = key.getInteger();
    if((0 < k) && (k <= MAXASIZE)) {
      logtable[luaO_ceillog2(k)]++;
    }
  }
}

void LuaTable::computeOptimalSizes(LuaValue newkey, int& outArraySize, int& outHashSize) {
  int totalKeys = 0;
  int logtable[32];

  memset(logtable,0,32*sizeof(int));

  for(int i = 0; i < (int)array_.size(); i++) {
    if(array_[i].isNil()) continue;
    // C index -> Lua index
    LuaValue key(i+1);
    countKey(key, logtable);
    totalKeys++;
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    LuaValue& key = hash_[i].i_key;
    LuaValue& val = hash_[i].i_val;
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

// #TODO - LuaTable resize should be effectively atomic...

int LuaTable::resize(int nasize, int nhsize) {
  int oldasize = (int)array_.size();
  //int oldhsize = (int)hash_.size();

  // Allocate temporary storage for the resize before we modify the table
  LuaVector<Node> temphash;
  LuaVector<LuaValue> temparray;

  if(nasize) {
    temparray.resize_nocheck(nasize);
    memcpy(temparray.begin(), array_.begin(), std::min(oldasize, nasize) * sizeof(LuaValue));
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
      set(LuaValue(i+1), temparray[i]);
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

int LuaTable::traverse(LuaTable::nodeCallback c, void* blob) {
  LuaValue temp;

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

void LuaTable::VisitGC(LuaGCVisitor& visitor) {
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

int LuaTable::PropagateGC(LuaGCVisitor& visitor) {
  visitor.MarkObject(metatable);

  bool weakkey = false;
  bool weakval = false;

  if(metatable) {
    LuaValue mode = metatable->get("__mode");

    if(mode.isString()) {
      weakkey = (strchr(mode.getString()->c_str(), 'k') != NULL);
      weakval = (strchr(mode.getString()->c_str(), 'v') != NULL);
      assert(weakkey || weakval);
    }
  }

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

int LuaTable::PropagateGC_Strong(LuaGCVisitor& visitor) {
  setColor(BLACK);

  for(int i = 0; i < (int)array_.size(); i++) {
    visitor.MarkValue(array_[i]);
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    if(n.i_val.isNil()) {
      if (n.i_key.isWhite()) {
        n.i_key = LuaValue::Nil();
      }
    } else {
      visitor.MarkValue(n.i_key);
      visitor.MarkValue(n.i_val);
    }
  }

  return TRAVCOST + (int)array_.size() + 2 * (int)hash_.size();
}

//----------

int LuaTable::PropagateGC_WeakValues(LuaGCVisitor& visitor) {
  bool hasDeadValues = false;

  for(int i = 0; i < (int)array_.size(); i++) {
    if(array_[i].isLiveColor()) hasDeadValues = true;
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    // Sweep dead keys with no values, mark all other
    // keys.
    if(n.i_val.isNil() && n.i_key.isWhite()) {
      n.i_key = LuaValue::Nil();
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

int LuaTable::PropagateGC_Ephemeron(LuaGCVisitor& visitor) {
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
        n.i_key = LuaValue::Nil();
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

void LuaTable::SweepWhite() {
  for (int i = 0; i < (int)array_.size(); i++) {
    if (array_[i].isLiveColor()) {
      array_[i] = LuaValue::Nil();
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];

    if(n.i_key.isLiveColor()) {
      n.i_key = LuaValue::Nil();
      n.i_val = LuaValue::Nil();
    }

    if(n.i_val.isLiveColor()) {
      n.i_val = LuaValue::Nil();
    }
  }
}

//----------

void LuaTable::SweepWhiteKeys() {
  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];
    if(n.i_key.isLiveColor()) {
      n.i_val = LuaValue::Nil();
      n.i_key = LuaValue::Nil();
    }
  }
}

//----------

void LuaTable::SweepWhiteVals() {
  for (int i = 0; i < (int)array_.size(); i++) {
    if (array_[i].isLiveColor()) {
      array_[i] = LuaValue::Nil();
    }
  }

  for(int i = 0; i < (int)hash_.size(); i++) {
    Node& n = hash_[i];
    if(!n.i_val.isLiveColor()) continue;

    // White value. If key was white, key goes away too.
    n.i_val = LuaValue::Nil();
    if (n.i_key.isWhite()) {
      n.i_key = LuaValue::Nil();
    }
  }
}

//-----------------------------------------------------------------------------
