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
** main search function
*/

const TValue* luaH_get2(Table* t, const TValue* key) {
  const TValue* result1 = t->findValue(*key);
  TValue result2 = t->get(*key);

  if(result1 == NULL) {
    if(!result2.isNone()) {
      printf("xxx");
    }
  } else if(result1->isNil()) {
    if(!result2.isNil()) {
      printf("xxx");
    }
  }

  return result1;
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
  if(t->set(TValue(key),*value)) {
    luaC_barrierback(t, *value);
  } else {
    luaG_runerror("Key is invalid (either nil or NaN)");
  }
}

