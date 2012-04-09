#pragma once
#include "LuaTypes.h"
#include "LuaBuffer.h"
#include "LuaGraylist.h"
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

  void incTotalBytes(int size);
  void setGCDebt(size_t debt);
  int  getGCDebt() { return GCdebt_; }
  void incGCDebt(int debt);

  stringtable* strings_;  /* hash table for strings */

  TValue l_registry;
  Table* getRegistry() { return l_registry.getTable(); }

  size_t lastmajormem;  /* memory in use after last major collection */
  LuaObject::Color livecolor;
  LuaObject::Color deadcolor;

  uint8_t gcstate;  /* state of garbage collector */
  uint8_t gckind;  /* kind of GC running */
  uint8_t gcrunning;  /* true if GC is running */

  LuaObject *allgc;  /* list of all collectable objects */
  LuaObject *finobj;  /* list of collectable objects with finalizers */
  LuaObject **sweepgc;  /* current position of sweep */

  // Gray lists
  LuaGraylist grayhead_;  // Topmost list of gray objects

  LuaGraylist grayagain_; // list of objects to be traversed atomically
  LuaGraylist weak_;      // list of tables with weak values
  LuaGraylist ephemeron_; // list of ephemeron tables (weak keys)
  LuaGraylist allweak_;   // list of all-weak tables

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

  uint8_t isShuttingDown;

  void PushGray(LuaObject* o);
  void PushGrayAgain(LuaObject* o);
  void PushWeak(LuaObject* o);
  void PushAllWeak(LuaObject* o);
  void PushEphemeron(LuaObject* o);

private:

  size_t totalbytes_;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt_;  /* bytes allocated not yet compensated by the collector */
};


stringtable* getGlobalStringtable();
