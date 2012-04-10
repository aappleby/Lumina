/*
** $Id: ltable.c,v 2.67 2011/11/30 12:41:45 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest `n' such that at
** least half the slots between 0 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the `original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <string.h>
#include <new>

#define ltable_c

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"

#include <float.h>
#include <math.h>

/*
** max size of array part is 2^MAXBITS
*/
#define MAXBITS		30
#define MAXASIZE	(1 << MAXBITS)

/*
** {=============================================================
** Rehash
** ==============================================================
*/

void countKey( TValue key, int* logtable ) {
  if(key.isInteger()) {
    int k = key.getInteger();
    if((0 < k) && (k <= MAXASIZE)) {
      logtable[luaO_ceillog2(k)]++;
    }
  }
}

void findBestArraySize( Table* t,
                       TValue newkey,
                       int & outArraySize,
                       int & outHashSize ) {

  int totalKeys = 0;
  int logtable[32];

  memset(logtable,0,32*sizeof(int));

  for(int i = 0; i < (int)t->array.size(); i++) {
    if(t->array[i].isNil()) continue;
    // C index -> Lua index
    TValue key(i+1);
    countKey(key, logtable);
    totalKeys++;
  }

  for(int i = 0; i < (int)t->hashtable.size(); i++) {
    TValue& key = t->hashtable[i].i_key;
    TValue& val = t->hashtable[i].i_val;
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

  outArraySize = bestSize;
  outHashSize = totalKeys - bestCount;
}

// Note - new memory for array & hash _must_ be allocated before we start moving things around,
// otherwise the allocation could trigger a GC pass which would try and traverse this table while
// it's in an invalid state.

// #TODO - Table resize should be effectively atomic...

void luaH_resize (Table *t, int nasize, int nhsize) {
  int oldasize = (int)t->array.size();
  int oldhsize = (int)t->hashtable.size();

  // Allocate temporary storage for the resize before we modify the table
  LuaVector<Node> temphash;
  LuaVector<TValue> temparray;

  if(nasize) {
    temparray.resize(nasize);
    memcpy(temparray.begin(), t->array.begin(), std::min(oldasize, nasize) * sizeof(TValue));
  }

  if (nhsize) {
    int lsize = luaO_ceillog2(nhsize);
    nhsize = 1 << lsize;
    temphash.resize(nhsize);
  }

  // Memory allocated, swap and reinsert

  temparray.swap(t->array);
  temphash.swap(t->hashtable);
  t->lastfree = (int)t->hashtable.size(); // all positions are free

  // Temparray now contains the old contents of array. If temparray is
  // larger than array, move the overflow to the hash table.
  if (temparray.size() > t->array.size()) {
    for(int i = (int)t->array.size(); i < (int)temparray.size(); i++) {
      if (!temparray[i].isNil()) {
        luaH_setint(t, i + 1, &temparray[i]);
      }
    }
  }
  // And finally re-insert the saved nodes.
  for (int i = (int)temphash.size() - 1; i >= 0; i--) {
    Node* old = &temphash[i];
    if (!old->i_val.isNil()) {
      luaH_set2(t, old->i_key, old->i_val);
    }
  }
}


static void rehash (Table *t, const TValue *ek) {
  int bestArraySize, bestHashSize;
  findBestArraySize(t, *ek, bestArraySize, bestHashSize);
  luaH_resize(t, bestArraySize, bestHashSize);
}



/*
** }=============================================================
*/

static Node *getfreepos (Table *t) {
  while (t->lastfree > 0) {
    t->lastfree--;
    Node* last = t->getNode(t->lastfree);
    if (last->i_key.isNil())
      return last;
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/
TValue *luaH_newkey (Table *t, const TValue *key) {
  if (key->isNil()) {
    //luaG_runerror("table index is nil");
    return NULL;
  }

  if (key->isNumber()) {
    double n = key->getNumber();
    if(n != n) {
      //luaG_runerror("table index is NaN");
      return NULL;
    }
  }
  
  Node* mp = t->findBin(*key);

  if(mp && mp->i_val.isNil()) {
    mp->i_key = *key;
    luaC_barrierback(t, *key);
    assert(mp->i_val.isNil());
    return &mp->i_val;
  }

  if ((mp == NULL) || !mp->i_val.isNil()) {  /* main position is taken? */
    Node *n = t->getFreeNode();  /* get a free place */
    if (n == NULL) {  /* cannot find a free place? */
      rehash(t, key);  /* grow table */
      /* whatever called 'newkey' take care of TM cache and GC barrier */
      return luaH_set(t, key);  /* insert key into grown table */
    }
    assert(n);

    Node* othern = t->findBin(mp->i_key);
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern->next != mp) othern = othern->next;  /* find previous */
      othern->next = n;  /* redo the chain with `n' in place of `mp' */
      *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      mp->next = NULL;  /* now `mp' is free */
      mp->i_val.clear();
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      n->next = mp->next;  /* chain new position */
      mp->next = n;
      mp = n;
    }
  }
  mp->i_key = *key;
  luaC_barrierback(t, *key);
  assert(mp->i_val.isNil());
  return &mp->i_val;
}


/*
** search function for integers
*/
const TValue *luaH_getint2 (Table *t, int key) {
  return t->findValue(key);
}

/*
** main search function
*/

const TValue *luaH_get2 (Table *t, const TValue *key) {
  if(key->isNil()) return NULL;
  
  if(key->isInteger()) {
    return t->findValue(key->getInteger());
  }

  if(key->isString()) {
    return t->findValueInHash(*key);
  }

  for(Node* n = t->findBin(*key); n; n = n->next) {
    if(n->i_key == *key) return &n->i_val;
  }

  return NULL;
}

const TValue *luaH_get (Table *t, const TValue *key) {
  const TValue* result = luaH_get2(t, key);
  return result ? result : luaO_nilobject;
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
TValue *luaH_set (Table *t, const TValue *key) {
  const TValue *p = luaH_get2(t, key);
  if (p) {
    return cast(TValue *, p);
  }
  else {
    TValue* result = luaH_newkey(t, key);
    if(result == NULL) {
      luaG_runerror("Key is invalid (either nil or NaN)");
    }
    return result;
  }
}

void luaH_set2 (Table *t, TValue key, TValue val) {
  const TValue *p = luaH_get2(t, &key);
  if (p) {
    *(TValue*)p = val;
  }
  else {
    const TValue* result = luaH_newkey(t, &key);
    if(result == NULL) {
      luaG_runerror("Key is invalid (either nil or NaN)");
    }
    *(TValue*)result = val;
  }
}


void luaH_setint (Table *t, int key, TValue *value) {
  const TValue *p = luaH_getint2(t, key);
  TValue *cell;
  if (p) {
    cell = cast(TValue *, p);
  }
  else {
    TValue k = TValue(key);
    cell = luaH_newkey(t, &k);
    if(cell == NULL) {
      luaG_runerror("Key is invalid (either nil or NaN)");
    }
  }
  *cell = *value;
}

static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find `i' and `j' such that i is present and j is not */
  while (t->findValue(j)) {
    i = j;
    j *= 2;
    if (j > cast(unsigned int, MAX_INT)) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (t->findValue(i)) i++;
      return i - 1;
    }
  }
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (t->findValue(m) == NULL) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table `t'. A `boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
int luaH_getn (Table *t) {
  unsigned int j = (unsigned int)t->array.size();
  if (j > 0 && t->array[j-1].isNil()) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (t->array[m-1].isNil()) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (t->hashtable.empty())  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}