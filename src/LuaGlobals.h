#pragma once
#include "LuaTypes.h"
#include "LuaBuffer.h"
#include "LuaUpval.h" // for uvhead
#include "LuaValue.h" // for l_registry

/*
** `global state', shared by all threads of this state
*/
class global_State {
public:
  size_t totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  size_t lastmajormem;  /* memory in use after last major collection */
  stringtable* strt;  /* hash table for strings */
  TValue l_registry;
  uint8_t currentwhite;
  uint8_t gcstate;  /* state of garbage collector */
  uint8_t gckind;  /* kind of GC running */
  uint8_t gcrunning;  /* true if GC is running */
  int sweepstrgc;  /* position of sweep in `strt' */
  LuaObject *allgc;  /* list of all collectable objects */
  LuaObject *finobj;  /* list of collectable objects with finalizers */
  LuaObject **sweepgc;  /* current position of sweep */
  LuaObject *gray;  /* list of gray objects */
  LuaObject *grayagain;  /* list of objects to be traversed atomically */
  LuaObject *weak;  /* list of tables with weak values */
  LuaObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  LuaObject *allweak;  /* list of all-weak tables */
  LuaObject *tobefnz;  /* list of userdata to be GC */
  UpVal uvhead;  /* head of double-linked list of all open upvalues */
  Mbuffer buff;  /* temporary buffer for string concatenation */
  int gcpause;  /* size of pause between successive GCs */
  int gcmajorinc;  /* how much to wait for a major GC (only in gen. mode) */
  int gcstepmul;  /* GC `granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names */
  Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
};


stringtable* getGlobalStringtable();