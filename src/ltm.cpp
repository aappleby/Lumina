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

Table* lua_getmetatable (const TValue* o);

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
    thread_G->tmname[i] = luaS_new(luaT_eventname[i]);
    thread_G->tmname[i]->setFixed();  /* never collect these names */
  }
}


const TValue *luaT_gettmbyobj (const TValue *o, TMS event) {
  Table* mt = lua_getmetatable(o);
  if(mt == NULL) return luaO_nilobject;
  TValue temp(thread_G->tmname[event]);
  return luaH_get(mt, &temp);
}

const TValue* fasttm ( Table* table, TMS tag) {
  if(table == NULL) return NULL;

  TValue temp(thread_G->tmname[tag]);
  const TValue *tm = luaH_get2(table, &temp);

  assert(tag <= TM_EQ);
  if ((tm == NULL) || tm->isNil()) {  /* no tag method? */
    return NULL;
  }
  else return tm;
}
