/*
** $Id: ltm.c,v 2.14 2011/06/02 19:31:40 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/


#include <string.h>

#define ltm_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


const char *const luaT_typenames_[LUA_TOTALTAGS] = {
  "no value",
  "nil",
  "boolean",
  "userdata",
  "number",
  "string",
  "table",
  "function",
  "userdata",
  "thread",
  /* these last two cases are used for tests only */
  "proto",
  "upval"
};

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

const char* ttypename(int tag) {
  return luaT_typenames_[tag + 1];
}

const char* objtypename(const TValue* v) {
  return luaT_typenames_[v->basetype() + 1];
}



void luaT_init() {
  int i;
  for (i=0; i<TM_N; i++) {
    thread_G->tmname[i] = luaS_new(luaT_eventname[i]);
    luaS_fix(thread_G->tmname[i]);  /* never collect these names */
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/
const TValue *luaT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = luaH_getstr(events, ename);
  assert(event <= TM_EQ);
  if (tm->isNil()) {  /* no tag method? */
    events->flags |= cast_byte(1u<<event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}


const TValue *luaT_gettmbyobj (const TValue *o, TMS event) {
  Table *mt;
  switch (ttypenv(o)) {
    case LUA_TTABLE:
      mt = hvalue(o)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = thread_G->mt[ttypenv(o)];
  }
  if(mt == NULL) return luaO_nilobject;
  return luaH_getstr(mt, thread_G->tmname[event]);
}

const TValue* fasttm ( Table* table, TMS tag) {
  if(table == NULL) return NULL;
  if(table->flags & (1<<tag)) return NULL;
  return luaT_gettm(table, tag, thread_G->tmname[tag]);
}
