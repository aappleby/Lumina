#include "LuaTable.h"

void getTableMode(Table* t, bool& outWeakKey, bool& outWeakVal);
int luaO_ceillog2 (unsigned int x);

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

Table::Table() : LuaObject(LUA_TTABLE) {
  metatable = NULL;
}

//-----------------------------------------------------------------------------

Node* Table::findNode(TValue key) {
  if(hashtable.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

Node* Table::findNode(int key) {
  if(hashtable.empty()) return NULL;

  for(Node* node = findBin(key); node; node = node->next) {
    if(node->i_key == key) return node;
  }

  return NULL;
}

//-----------------------------------------------------------------------------

Node* Table::findBin(TValue key) {
  if(hashtable.empty()) return NULL;

  uint32_t hash = key.hashValue();
  uint32_t mask = (uint32_t)hashtable.size() - 1;

  return &hashtable[hash & mask];
}

Node* Table::findBin(int key) {
  if(hashtable.empty()) return NULL;

  uint32_t hash = hash64(key, 0);
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
  
  outIndex = (int)(node - hashtable.begin()) + (int)array.size();
  return true;
}

bool Table::tableIndexToKeyVal(int index, TValue& outKey, TValue& outVal) {
  if(index < 0) return false;
  if(index < (int)array.size()) {
    outKey = TValue(index + 1); // c index -> lua index
    outVal = array[index];
    return true;
  }

  index -= (int)array.size();
  if(index < (int)hashtable.size()) {
    outKey = hashtable[index].i_key;
    outVal = hashtable[index].i_val;
    return true;
  }

  return false;
}

const TValue* Table::findValue(TValue key) {

  if(key.isNil()) return NULL;

  if(key.isInteger()) {
    // lua index -> c index
    int index = key.getInteger() - 1;
    if((index >= 0) && (index < (int)array.size())) {
      return &array[index];
    } else {
      return findValueInHash(key.getInteger());
    }
  }

  return findValueInHash(key);
}

const TValue* Table::findValueInHash(TValue key) {
  Node* node = findNode(key);
  return node ? &node->i_val : NULL;
}

const TValue* Table::findValueInHash(int key) {
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

//-----------------------------------------------------------------------------

Node* Table::nodeAt(uint32_t hash) {
  if(hashtable.empty()) return NULL;

  uint32_t mask = (uint32_t)hashtable.size() - 1;
  return &hashtable[hash & mask];
}

//-----------------------------------------------------------------------------

Node* Table::getFreeNode() {
  while (lastfree > 0) {
    lastfree--;
    Node* last = &hashtable[lastfree];
    if (last->i_key.isNil())
      return last;
  }
  return NULL;
}

//-----------------------------------------------------------------------------

// caller has to call luaC_barrierback(t, *key);
//
// inserts a new key into a hash table; first, check whether key's main
// position is free. If not, check whether colliding node is in its main
// position or not: if it is not, move colliding node to an empty place and
// put new key in its main position; otherwise (colliding node is in its main
// position), new key goes to an empty position.
//
/*
TValue* Table::newKey(const TValue *key) {
  if (key->isNil()) {
    luaG_runerror("table index is nil");
    return NULL;
  }

  if (key->isNumber()) {
    double n = key->getNumber();
    if(n != n) {
      luaG_runerror("table index is NaN");
      return NULL;
    }
  }
  
  Node* mp = findBin(*key);

  if(mp && mp->i_val.isNil()) {
    mp->i_key = *key;
    //luaC_barrierback(t, *key);
    assert(mp->i_val.isNil());
    return &mp->i_val;
  }

  if ((mp == NULL) || !mp->i_val.isNil()) {  // main position is taken?
    Node *n = getFreeNode();  // get a free place
    if (n == NULL) {  // cannot find a free place?
      rehash(t, key);  // grow table
      // whatever called 'newkey' take care of TM cache and GC barrier
      return luaH_set(t, key);  // insert key into grown table
    }
    assert(n);

    Node* othern = t->findBin(mp->i_key);
    if (othern != mp) {  // is colliding node out of its main position?
      // yes; move colliding node into free position
      while (othern->next != mp) othern = othern->next;  // find previous
      othern->next = n;  // redo the chain with `n' in place of `mp'
      *n = *mp;  // copy colliding node into free pos. (mp->next also goes)
      mp->next = NULL;  // now `mp' is free
      mp->i_val.clear();
    }
    else {  // colliding node is in its own main position
      // new node will go into free position
      n->next = mp->next;  // chain new position
      mp->next = n;
      mp = n;
    }
  }
  mp->i_key = *key;
  //luaC_barrierback(t, *key);
  assert(mp->i_val.isNil());
  return &mp->i_val;
}
*/

//-----------------------------------------------------------------------------

/*
void Table::resize(int nasize, int nhsize) {
  int oldasize = (int)array.size();
  int oldhsize = (int)hashtable.size();

  // Allocate temporary storage for the resize before we modify the table
  LuaVector<Node> temphash;
  LuaVector<TValue> temparray;

  if(nasize) {
    temparray.resize(nasize);
    memcpy(temparray.begin(), array.begin(), std::min(oldasize, nasize) * sizeof(TValue));
  }

  if (nhsize) {
    int lsize = luaO_ceillog2(nhsize);
    nhsize = 1 << lsize;
    temphash.resize(nhsize);
  }

  // Memory allocated, swap and reinsert

  temparray.swap(array);
  temphash.swap(hashtable);
  lastfree = (int)hashtable.size(); // all positions are free

  // Temparray now contains the old contents of array. If temparray is
  // larger than array, move the overflow to the hash table.
  if (temparray.size() > array.size()) {
    for(int i = (int)array.size(); i < (int)temparray.size(); i++) {
      if (!temparray[i].isNil()) {
        luaH_setint(this, i + 1, &temparray[i]);
      }
    }
  }
  // And finally re-insert the saved nodes.
  for (int i = (int)temphash.size() - 1; i >= 0; i--) {
    Node* old = &temphash[i];
    if (!old->i_val.isNil()) {
      TValue* key = &old->i_key;
      TValue* val = &old->i_val;
      TValue* n = luaH_set(this, key);
      *n = old->i_val;
    }
  }
}
*/

//-----------------------------------------------------------------------------

int Table::traverseNodes(Table::nodeCallback c, void* blob) {
  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);
    c(&n->i_key, &n->i_val, blob);
  }

  return TRAVCOST + 2 * (int)hashtable.size();
}

int Table::traverseArray(Table::valueCallback c, void* blob) {
  for(int i = 0; i < (int)array.size(); i++) {
    c(&array[i],blob);
  }

  return TRAVCOST + (int)array.size();
}

int Table::traverse(Table::nodeCallback c, void* blob) {
  TValue temp;

  for(int i = 0; i < (int)array.size(); i++) {
    temp = i + 1; // c index -> lua index;
    c(&temp,&array[i],blob);
  }
  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);
    c(&n->i_key, &n->i_val, blob);
  }

  return TRAVCOST + (int)array.size() + 2 * (int)hashtable.size();
}

//-----------------------------------------------------------------------------

void Table::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);

  for(int i = 0; i < (int)array.size(); i++) {
    if(array[i].isString()) {
      array[i].getObject()->setColor(LuaObject::GRAY);
    }
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);

    if(n->i_key.isString()) n->i_key.getObject()->setColor(LuaObject::GRAY);
    if(n->i_val.isString()) n->i_val.getObject()->setColor(LuaObject::GRAY);
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

  for(int i = 0; i < (int)array.size(); i++) {
    visitor.MarkValue(array[i]);
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);

    if(n->i_val.isNil()) {
      if (n->i_key.isWhite()) {
        n->i_key = TValue::Nil();
      }
    } else {
      visitor.MarkValue(n->i_key);
      visitor.MarkValue(n->i_val);
    }
  }

  return TRAVCOST + (int)array.size() + 2 * (int)hashtable.size();
}

//----------

int Table::PropagateGC_WeakValues(GCVisitor& visitor) {
  bool hasDeadValues = false;

  for(int i = 0; i < (int)array.size(); i++) {
    if(array[i].isLiveColor()) hasDeadValues = true;
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);

    // Sweep dead keys with no values, mark all other
    // keys.
    if(n->i_val.isNil() && n->i_key.isWhite()) {
      n->i_key = TValue::Nil();
    } else {
      visitor.MarkValue(n->i_key);
    }

    if(n->i_val.isLiveColor()) hasDeadValues = true;
  }

  if (hasDeadValues) {
    visitor.PushWeak(this);
  }
  else {
    visitor.PushGrayAgain(this);
  }

  return TRAVCOST + (int)hashtable.size();
}

//----------
// weak keys, strong values

int Table::PropagateGC_Ephemeron(GCVisitor& visitor) {
  bool propagate = false;
  bool hasDeadKeys = false;

  for(int i = 0; i < (int)array.size(); i++) {
    if (array[i].isWhite()) {
      visitor.MarkValue(array[i]);
    }
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);

    // sweep keys for nil values
    if (n->i_val.isNil()) {
      if (n->i_key.isWhite()) {
        n->i_key = TValue::Nil();
      }
      continue;
    }

    if (n->i_key.isLiveColor()) {
      hasDeadKeys = true;
     
      if (n->i_val.isLiveColor()) {
        propagate = true;
      }
    } else {
      // Key is marked, mark the value if it's white.
      if (n->i_val.isWhite()) {
        visitor.MarkValue(n->i_val);
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

  return TRAVCOST + (int)array.size() + (int)hashtable.size();
}

//----------

void Table::SweepWhite() {
  for (int i = 0; i < (int)array.size(); i++) {
    if (array[i].isLiveColor()) {
      array[i] = TValue::Nil();
    }
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);

    if(n->i_key.isLiveColor()) {
      n->i_key = TValue::Nil();
      n->i_val = TValue::Nil();
    }

    if(n->i_val.isLiveColor()) {
      n->i_val = TValue::Nil();
    }
  }
}

//----------

void Table::SweepWhiteKeys() {
  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);
    if(n->i_key.isLiveColor()) {
      n->i_val = TValue::Nil();
      n->i_key = TValue::Nil();
    }
  }
}

//----------

void Table::SweepWhiteVals() {
  for (int i = 0; i < (int)array.size(); i++) {
    if (array[i].isLiveColor()) {
      array[i] = TValue::Nil();
    }
  }

  for(int i = 0; i < (int)hashtable.size(); i++) {
    Node* n = getNode(i);
    if(!n->i_val.isLiveColor()) continue;

    // White value. If key was white, key goes away too.
    n->i_val = TValue::nil;
    if (n->i_key.isWhite()) {
      n->i_key = TValue::Nil();
    }
  }
}

//-----------------------------------------------------------------------------
