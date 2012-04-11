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
#include <conio.h>

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
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** }=============================================================
*/



/*
** search function for integers
*/
const TValue *luaH_getint2 (Table *t, int key) {
  return t->findValue(key);
}

/*
** main search function
*/

const TValue* luaH_get2(Table* t, const TValue* key) {
  return t->findValue(*key);
}

void luaH_set2 (Table *t, TValue key, TValue val) {
  if(t->set(key,val)) {
    luaC_barrierback(t, key);
    luaC_barrierback(t, val);
  } else {
    luaG_runerror("Key is invalid (either nil or NaN)");
  }
}

void luaH_setint (Table *t, int key, TValue *value) {
  if(t->set(key,*value)) {
    luaC_barrierback(t, *value);
  } else {
    luaG_runerror("Key is invalid (either nil or NaN)");
  }
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