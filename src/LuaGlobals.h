#pragma once
#include "LuaTypes.h"
#include "LuaBuffer.h"
#include "LuaUpval.h" // for uvhead
#include "LuaValue.h" // for l_registry

/*
** `global state', shared by all threads of this state
*/
class global_State : public LuaBase {
public:

  global_State();
  ~global_State();

  // actual number of total bytes allocated
  size_t getTotalBytes() {
    return totalbytes_;
  }

  void setGCDebt(size_t debt) {
    GCdebt_ = debt;
  }

  int getGCDebt() { return GCdebt_; }

  void incGCDebt(int debt) { 
    totalbytes_ += debt;
    GCdebt_ += debt;
  }

  stringtable* strings_;  /* hash table for strings */

  TValue l_registry;
  Table* getRegistry() { return l_registry.getTable(); }

  size_t lastmajormem;  /* memory in use after last major collection */
  LuaObject::Color livecolor;
  LuaObject::Color deadcolor;

  uint8_t gcstate;  /* state of garbage collector */
  uint8_t gckind;  /* kind of GC running */
  uint8_t gcrunning;  /* true if GC is running */
  int sweepstrgc;  /* position of sweep in `strt' */

  LuaObject *allgc;  /* list of all collectable objects */
  LuaObject *finobj;  /* list of collectable objects with finalizers */
  LuaObject **sweepgc;  /* current position of sweep */

  LuaObject *grayhead_;  /* list of gray objects */

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
  TString *tagmethod_names_[TM_N];  /* array with tag-method names */
  Table *base_metatables_[LUA_NUMTAGS];  /* metatables for basic types */

private:

  size_t totalbytes_;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt_;  /* bytes allocated not yet compensated by the collector */
};


stringtable* getGlobalStringtable();
