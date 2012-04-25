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

LuaTable* lua_getmetatable (LuaValue v);

LuaValue luaT_gettmbyobj2 (LuaValue v, TMS event) {
  LuaTable* mt = lua_getmetatable(v);
  if(mt == NULL) return LuaValue::None();
  LuaValue temp(thread_G->tagmethod_names_[event]);
  return mt->get(temp);
}

LuaValue fasttm2 ( LuaTable* table, TMS tag) {
  if(table == NULL) return LuaValue::None();

  LuaValue temp(thread_G->tagmethod_names_[tag]);
  LuaValue tm = table->get(temp);

  assert(tag <= TM_EQ);
  if (tm.isNone() || tm.isNil()) {  /* no tag method? */
    return LuaValue::None();
  }
  else return tm;
}
