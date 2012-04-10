/*
** $Id: ltm.c,v 2.14 2011/06/02 19:31:40 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaUserdata.h"

#include <string.h>

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"

Table* lua_getmetatable (TValue v);

/* ORDER TM */
static const char *const luaT_eventname[] = {
  "__index",
  "__newindex",
  "__gc",
  "__mode",
  "__len",
  "__eq",
  "__add",
  "__sub",
  "__mul",
  "__div",
  "__mod",
  "__pow",
  "__unm",
  "__lt",
  "__le",
  "__concat",
  "__call"
};

void luaT_init() {
  int i;
  for (i=0; i<TM_N; i++) {
    thread_G->tagmethod_names_[i] = luaS_new(luaT_eventname[i]);
    thread_G->tagmethod_names_[i]->setFixed();  /* never collect these names */
  }
}


const TValue *luaT_gettmbyobj (const TValue *o, TMS event) {
  Table* mt = lua_getmetatable(*o);
  if(mt == NULL) return luaO_nilobject;
  TValue temp(thread_G->tagmethod_names_[event]);
  return luaH_get(mt, &temp);
}

TValue luaT_gettmbyobj2 (TValue v, TMS event) {
  Table* mt = lua_getmetatable(v);
  if(mt == NULL) return TValue::None();
  TValue temp(thread_G->tagmethod_names_[event]);
  return mt->get(temp);
}

const TValue* fasttm ( Table* table, TMS tag) {
  if(table == NULL) return NULL;

  TValue temp(thread_G->tagmethod_names_[tag]);
  const TValue *tm = luaH_get2(table, &temp);

  assert(tag <= TM_EQ);
  if ((tm == NULL) || tm->isNil()) {  /* no tag method? */
    return NULL;
  }
  else return tm;
}

TValue fasttm2 ( Table* table, TMS tag) {
  if(table == NULL) return TValue::None();

  TValue temp(thread_G->tagmethod_names_[tag]);
  TValue tm = table->get(temp);

  assert(tag <= TM_EQ);
  if (tm.isNone() || tm.isNil()) {  /* no tag method? */
    return TValue::None();
  }
  else return tm;
}

/*
const TValue* fasttm(Table* table, TMS tag) {
  const TValue* oldtm= fasttm_old(table, tag);
  TValue newtm = fasttm_new(table, tag);

  if(oldtm == NULL) {
    assert(newtm.isNone());
    return oldtm;
  } else {
    assert(!oldtm->isNil());
    assert(!newtm.isNil());
    return oldtm;
  }
}

TValue fasttm2(Table* table, TMS tag) {
  const TValue* oldtm= fasttm_old(table, tag);
  TValue newtm = fasttm_new(table, tag);

  if(oldtm == NULL) {
    assert(newtm.isNone());
    return newtm;
  } else {
    assert(!oldtm->isNil());
    assert(!newtm.isNil());
    return newtm;
  }
}
*/