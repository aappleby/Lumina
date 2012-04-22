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
#include "ltm.h"

Table* lua_getmetatable (TValue v);

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
