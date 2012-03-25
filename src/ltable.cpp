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

const TValue *luaH_getint_hash (Table *t, int key);
void luaH_setint_hash (Table *t, int key, TValue *value);

/*
** max size of array part is 2^MAXBITS
*/
#define MAXBITS		30
#define MAXASIZE	(1 << MAXBITS)

int luaH_next (Table *t, StkId stack) {
  int start = -1;

  if(!stack[0].isNil()) {
    bool found = t->keyToTableIndex(stack[0],start);
    if(!found) {
      luaG_runerror("invalid key to 'next'");
      return 0;
    }
  }

  for(int cursor = start+1; cursor < t->getTableIndexSize(); cursor++) {
    TValue key, val;
    if(t->tableIndexToKeyVal(cursor,key,val) && !val.isNil()) {
      stack[0] = key;
      stack[1] = val;
      return 1;
    }
  }

  return 0;
}

/*
** {=============================================================
** Rehash
** ==============================================================
*/


static int computesizes (int nums[], int *narray) {
  int i;
  int twotoi;  /* 2^i */
  int a = 0;  /* number of elements smaller than 2^i */
  int na = 0;  /* number of elements to go to array part */
  int n = 0;  /* optimal size for array part */
  for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        n = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements smaller than n will go to array part */
      }
    }
    if (a == *narray) break;  /* all elements already counted */
  }
  *narray = n;
  assert(*narray/2 <= na && na <= *narray);
  return na;
}


static int countint (const TValue *key, int *nums) {
  int k = key->isInteger() ? key->getInteger() : -1;
  if (0 < k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
    nums[luaO_ceillog2(k)]++;  /* count as such */
    return 1;
  }
  else
    return 0;
}


static int numusearray (Table *t, int *nums) {
  int lg;
  int ttlg;  /* 2^lg */
  int ause = 0;  /* summation of `nums' */
  int i = 1;  /* count to traverse all array keys */
  for (lg=0, ttlg=1; lg<=MAXBITS; lg++, ttlg*=2) {  /* for each slice */
    int lc = 0;  /* counter */
    int lim = ttlg;
    if (lim > (int)t->array.size()) {
      lim = (int)t->array.size();  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg-1), 2^lg] */
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}


static int numusehash (Table *t, int *nums, int *pnasize) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* summation of `nums' */
  int i = (int)t->hashtable.size();
  while (i--) {
    Node *n = t->getNode(i);
    if (!ttisnil(&n->i_val)) {
      ause += countint(&n->i_key, nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
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
  temphash.init();
  temparray.init();

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
        luaH_setint_hash(t, i + 1, &temparray[i]);
      }
    }
  }
  // And finally re-insert the saved nodes.
  for (int i = (int)temphash.size() - 1; i >= 0; i--) {
    Node* old = &temphash[i];
    if (!ttisnil(&old->i_val)) {
      TValue* key = &old->i_key;
      TValue* val = &old->i_val;
      TValue* n = luaH_set(t, key);
      setobj(n, val);
    }
  }
}


void luaH_resizearray (Table *t, int nasize) {
  int nsize = (int)t->hashtable.size();
  luaH_resize(t, nasize, nsize);
}


static void rehash (Table *t, const TValue *ek) {
  int nasize, na;
  int nums[MAXBITS+1];  /* nums[i] = number of keys with 2^(i-1) < k <= 2^i */
  int i;
  int totaluse;
  for (i=0; i<=MAXBITS; i++) nums[i] = 0;  /* reset counts */
  nasize = numusearray(t, nums);  /* count keys in array part */
  totaluse = nasize;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
  /* count extra key */
  nasize += countint(ek, nums);
  totaluse++;
  /* compute new size for array part */
  na = computesizes(nums, &nasize);
  /* resize the table to new computed sizes */
  luaH_resize(t, nasize, totaluse - na);
}



/*
** }=============================================================
*/

static Node *getfreepos (Table *t) {
  while (t->lastfree > 0) {
    t->lastfree--;
    Node* last = t->getNode(t->lastfree);
    if (ttisnil(&last->i_key))
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
  if (key->isNil()) luaG_runerror("table index is nil");
  else if (ttisnumber(key) && luai_numisnan(L, nvalue(key)))
    luaG_runerror("table index is NaN");
  
  Node* mp = t->findBin(*key);
  if ((mp == NULL) || !ttisnil(&mp->i_val)) {  /* main position is taken? */
    Node *othern;
    Node *n = getfreepos(t);  /* get a free place */
    if (n == NULL) {  /* cannot find a free place? */
      rehash(t, key);  /* grow table */
      /* whatever called 'newkey' take care of TM cache and GC barrier */
      return luaH_set(t, key);  /* insert key into grown table */
    }
    assert(n);
    othern = t->findBin(mp->i_key);
    Node* bak = othern;
    if(othern == NULL) {
      printf("xxx");
    }
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern->next != mp) othern = othern->next;  /* find previous */
      othern->next = n;  /* redo the chain with `n' in place of `mp' */
      *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      mp->next = NULL;  /* now `mp' is free */
      setnilvalue(&mp->i_val);
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      n->next = mp->next;  /* chain new position */
      mp->next = n;
      mp = n;
    }
  }
  setobj(&mp->i_key, key);
  luaC_barrierback(t, key);
  assert(ttisnil(&mp->i_val));
  return &mp->i_val;
}


/*
** search function for integers
*/
const TValue *luaH_getint (Table *t, int key) {
  size_t index = (size_t)key - 1;
  /* (1 <= key && key <= t->sizearray) */
  if (index < t->array.size())
    return &t->array[index];
  else {
    TValue temp(key);

    Node *n = t->findBin(temp);

    for(; n; n = n->next) {
      if (n->i_key.isNumber() && (n->i_key.n == key))
        return &n->i_val;
    }

    return luaO_nilobject;
  }
}

const TValue* luaH_get2(Table* t, TValue key) {
  for(Node* n = t->findBin(key); n; n = n->next) {
    if(n->i_key == key) return &n->i_val;
  }

  return luaO_nilobject;
}

const TValue *luaH_getint_hash (Table *t, int key) {
  TValue temp(key);
  return luaH_get2(t, temp);
}

const TValue *luaH_getstr (Table *t, TString *key) {
  TValue temp(key);
  return luaH_get2(t, TValue(key));
}

/*
** main search function
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttypenv(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, tsvalue(key));
    case LUA_TNUMBER: {
      int k;
      lua_Number n = nvalue(key);
      lua_number2int(k, n);
      if (luai_numeq(cast_num(k), nvalue(key))) /* index is int? */
        return luaH_getint(t, k);  /* use specialized version */
      /* else go through */
    }
    default: {
      Node *n = t->findBin(*key);
      if(n == NULL) return luaO_nilobject;

      do {  /* check whether `key' is somewhere in the chain */
        if (luaV_rawequalobj(&n->i_key, key))
          return &n->i_val;  /* that's it */
        else n = n->next;
      } while (n);
      return luaO_nilobject;
    }
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
TValue *luaH_set (Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else return luaH_newkey(t, key);
}


void luaH_setint (Table *t, int key, TValue *value) {
  const TValue *p = luaH_getint(t, key);
  TValue *cell;
  if (p != luaO_nilobject)
    cell = cast(TValue *, p);
  else {
    TValue k;
    setnvalue(&k, cast_num(key));
    cell = luaH_newkey(t, &k);
  }
  setobj(cell, value);
}

void luaH_setint_hash (Table *t, int key, TValue *value) {
  const TValue *p = luaH_getint_hash(t, key);
  TValue *cell;
  if (p != luaO_nilobject)
    cell = cast(TValue *, p);
  else {
    TValue k;
    setnvalue(&k, cast_num(key));
    cell = luaH_newkey(t, &k);
  }
  setobj(cell, value);
}

static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find `i' and `j' such that i is present and j is not */
  while (!ttisnil(luaH_getint(t, j))) {
    i = j;
    j *= 2;
    if (j > cast(unsigned int, MAX_INT)) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getint(t, i))) i++;
      return i - 1;
    }
  }
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getint(t, m))) j = m;
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
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (t->hashtable.empty())  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}