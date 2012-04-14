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
  //ScopedMemChecker c;
  for (int i=0; i<TM_N; i++) {
    thread_G->tagmethod_names_[i] = luaS_new(luaT_eventname[i]);
    thread_G->tagmethod_names_[i]->setFixed();  /* never collect these names */
  }
}


TValue luaT_gettmbyobj2 (TValue v, TMS event) {
  Table* mt = lua_getmetatable(v);
  if(mt == NULL) return TValue::None();
  TValue temp(thread_G->tagmethod_names_[event]);
  return mt->get(temp);
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
