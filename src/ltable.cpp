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
#define LUA_CORE

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

Node* hashpow2(const Table* t, uint32_t n) {
  if(t->sizenode == 0) return NULL;
  uint32_t mask = t->sizenode - 1;
  return &t->node[n & mask];
}

#define hashstr(t,str)   hashpow2(t, (str)->getHash())
#define hashboolean(t,p) hashpow2(t, p)

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

uint32_t hash32 (uint32_t a) {
  a ^= a >> 16;
  a *= 0x85ebca6b;
  a ^= a >> 13;
  a *= 0xc2b2ae35;
  a ^= a >> 16;

  return a;
}

static Node* hashpointer ( const Table* t, void* p ) {
  if(t->sizenode == 0) return NULL;
  uint32_t* block = reinterpret_cast<uint32_t*>(&p);
  uint32_t hash;
  if(sizeof(p) == 8) {
    hash = hash64(block[0],block[1]);
  } else {
    hash = hash32(block[0]);
  }
  uint32_t mask = t->sizenode - 1;
  return &t->node[hash & mask];
}

// Well damn, test suite goes from 21.5 to 18.9 seconds just by changing to
// this hash...
static Node* hashnum ( const Table* t, lua_Number n) {
  if(t->sizenode == 0) return NULL;
  uint32_t* block = reinterpret_cast<uint32_t*>(&n);
  uint32_t hash = hash64(block[0],block[1]);
  uint32_t mask = t->sizenode - 1;
  return &t->node[hash & mask];
}

/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Node *mainposition (const Table *t, const TValue *key) {
  if(t->sizenode == 0) return NULL;
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TSTRING:
      return hashstr(t, tsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}

/*
** returns the index of a `key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by -1.
*/
static int findindex (Table *t, TValue key) {
  if (key.isNil()) return -1;  /* first iteration */
  int i = key.isInteger() ? key.getInteger() : -1;
  
  if (0 < i && i <= (int)t->array.size())  /* is `key' inside array part? */
    return i-1;  /* yes; that's the index (corrected to C) */

  Node *n = mainposition(t, &key);
  if (n == NULL) {
    luaG_runerror("invalid key to 'next'");
  }

  for (;;) {
    bool equal = luaV_rawequalobj(&n->i_key, &key);
    if (equal || (ttisdeadkey(&n->i_key) && iscollectable(&key) && deadvalue(&n->i_key) == gcvalue(&key))) {
      return (int)(n - t->node) + (int)t->array.size();
    }
    else n = n->next;
    if (n == NULL)
      luaG_runerror("invalid key to 'next'");
  }
}


int luaH_next (Table *t, StkId stack) {
  if(t->array.empty() && (t->sizenode == 0)) return 0;

  int i = findindex(t, stack[0]) + 1;
  for (;i < (int)t->array.size(); i++) {
    if (!t->array[i].isNil()) {
      stack[0] = i+1;
      stack[1] = t->array[i];
      return 1;
    }
  }
  i -= (int)t->array.size();
  for (; i < t->sizenode; i++) {
    Node& n = t->node[i];
    if (!n.i_val.isNil()) {
      stack[0] = n.i_key;
      stack[1] = n.i_val;
      return 1;
    }
  }
  return 0;  /* no more elements */
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


static int numusearray (const Table *t, int *nums) {
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


static int numusehash (const Table *t, int *nums, int *pnasize) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* summation of `nums' */
  int i = t->sizenode;
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(&n->i_val)) {
      ause += countint(&n->i_key, nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
}


static void setarrayvector (Table *t, int size) {
  int oldsize = (int)t->array.size();
  
  t->array.resize(size);
  
  for (int i=oldsize; i < t->array.size(); i++)
     setnilvalue(&t->array[i]); 
}


static void setnodevector (Table *t, int size) {
  //assert((size & (size-1)) == 0);

  if (size == 0) {
    //t->node = cast(Node *, dummynode);  // use common `dummynode'
    //t->sizenode = 1;
    t->node = NULL;
    t->sizenode = 0;
    t->lastfree = &t->node[size]; // all positions are free
  }
  else {
    int lsize = luaO_ceillog2(size);
    size = 1 << lsize;
    t->node = (Node*)luaM_alloc(size * sizeof(Node));
    memset(t->node, 0, size * sizeof(Node));
    t->sizenode = size;
    t->lastfree = &t->node[size]; // all positions are free
  }
}


void luaH_resize (Table *t, int nasize, int nhsize) {
  int i;
  int oldasize = (int)t->array.size();
  int oldhsize = t->sizenode;
  Node *nold = t->node;  /* save old hash ... */
  if (nasize > oldasize)  /* array part must grow? */
    setarrayvector(t, nasize);
  /* create new hash part with appropriate size */
  setnodevector(t, nhsize);
  if (nasize < oldasize) {  /* array part must shrink? */
    // Move elements in the disappearing array section to the hash table.
    for(int i = nasize; i < oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        luaH_setint_hash(t, i + 1, &t->array[i]);
    }
    t->array.resize(nasize);
  }
  /* re-insert elements from hash part */
  for (i = oldhsize - 1; i >= 0; i--) {
    Node *old = nold+i;
    if (!ttisnil(&old->i_val)) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      TValue* key = &old->i_key;
      TValue* val = &old->i_val;
      TValue* n = luaH_set(t, key);
      setobj(n, val);
    }
  }
  if (nold) {
     /* free old array */
    size_t s = oldhsize;
    luaM_free(nold, s * sizeof(Node));
  }
}


void luaH_resizearray (Table *t, int nasize) {
  int nsize = t->sizenode;
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


Table *luaH_new () {
  void* newblock = luaM_newobject(LUA_TTABLE, sizeof(Table));

  Table* t = new(newblock) Table();
  t->Init(LUA_TTABLE);
  
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  t->array.init();
  setnodevector(t, 0);
  return t;
}


void luaH_free (Table *t) {
  if (t->node) {
    luaM_free(t->node, t->sizenode * sizeof(Node));
  }
  t->array.clear();
  luaM_delobject(t, sizeof(Table), LUA_TTABLE);
}


static Node *getfreepos (Table *t) {
  while (t->lastfree > t->node) {
    t->lastfree--;
    if (ttisnil(&t->lastfree->i_key))
      return t->lastfree;
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
  Node *mp;
  if (ttisnil(key)) luaG_runerror("table index is nil");
  else if (ttisnumber(key) && luai_numisnan(L, nvalue(key)))
    luaG_runerror("table index is NaN");
  mp = mainposition(t, key);
  if ((mp == NULL) || !ttisnil(&mp->i_val)) {  /* main position is taken? */
    Node *othern;
    Node *n = getfreepos(t);  /* get a free place */
    if (n == NULL) {  /* cannot find a free place? */
      rehash(t, key);  /* grow table */
      /* whatever called 'newkey' take care of TM cache and GC barrier */
      return luaH_set(t, key);  /* insert key into grown table */
    }
    assert(n);
    othern = mainposition(t, &mp->i_key);
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
  luaC_barrierback(obj2gco(t), key);
  assert(ttisnil(&mp->i_val));
  return &mp->i_val;
}


/*
** search function for integers
*/
const TValue *luaH_getint (Table *t, int key) {
  /* (1 <= key && key <= t->sizearray) */
  if (cast(unsigned int, key-1) < cast(unsigned int, t->array.size()))
    return &t->array[key-1];
  else {
    lua_Number nk = cast_num(key);
    Node *n = hashnum(t, nk);
    if(n == NULL) return luaO_nilobject;

    do {  /* check whether `key' is somewhere in the chain */
      if (ttisnumber(&n->i_key) && luai_numeq(nvalue(&n->i_key), nk))
        return &n->i_val;  /* that's it */
      else n = n->next;
    } while (n);
    return luaO_nilobject;
  }
}

const TValue *luaH_getint_hash (Table *t, int key) {
  /* (1 <= key && key <= t->sizearray) */
  lua_Number nk = cast_num(key);
  Node *n = hashnum(t, nk);
  if(n == NULL) return luaO_nilobject;

  do {  /* check whether `key' is somewhere in the chain */
    if (ttisnumber(&n->i_key) && luai_numeq(nvalue(&n->i_key), nk))
      return &n->i_val;  /* that's it */
    else n = n->next;
  } while (n);
  return luaO_nilobject;
}


/*
** search function for strings
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  if(n == NULL) return luaO_nilobject;

  do {  /* check whether `key' is somewhere in the chain */
    if (ttisstring(&n->i_key) && eqstr(tsvalue(&n->i_key), key))
      return &n->i_val;  /* that's it */
    else n = n->next;
  } while (n);
  return luaO_nilobject;
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
      Node *n = mainposition(t, key);
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
  else if (t->node == NULL)  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}
