#pragma once

#include "LuaBuffer.h"
#include "LuaCollector.h"
#include "LuaList.h"
#include "LuaUpval.h" // for uvhead
#include "LuaValue.h" // for l_registry

/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */
#define KGC_GEN		2	/* generational collection */


/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSatomic	1
#define GCSsweepstring	2
#define GCSsweepudata	3
#define GCSsweep	4
#define GCSpause	5

/* predefined values in the registry */
#define LUA_RIDX_MAINTHREAD	1
#define LUA_RIDX_GLOBALS	2
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS

struct LuaAnchor;

/*
** `global state', shared by all threads of this state
*/
class LuaVM : public LuaBase {
public:

  LuaVM();
  ~LuaVM();

  // actual number of total bytes allocated
  size_t getTotalBytes() {
    return totalbytes_;
  }

  /*
  ** macro to tell when main invariant (white objects cannot point to black
  ** ones) must be kept. During a non-generational collection, the sweep
  ** phase may break the invariant, as objects turned white may point to
  ** still-black objects. The invariant is restored when sweep ends and
  ** all objects are white again. During a generational collection, the
  ** invariant must be kept all times.
  */

  //----------

  bool isSweepPhase() {
    return (GCSsweepstring <= gcstate) && (gcstate <= GCSsweep);
  }

  bool keepInvariant() {
    return (gckind == KGC_GEN) || (gcstate <= GCSatomic);
  }

  //----------

  void incTotalBytes(int size);
  void setGCDebt(size_t debt);
  int  getGCDebt() { return (int)GCdebt_; }
  void incGCDebt(int debt);

  //----------

  LuaStringTable* strings_;  /* hash table for strings */

  LuaValue l_registry;
  LuaTable* getRegistry() { return l_registry.getTable(); }
  LuaTable* getRegistryTable(const char* name);

  LuaValue l_globals;
  LuaTable* getGlobals() { return l_globals.getTable(); }

  size_t lastmajormem;  /* memory in use after last major collection */
  LuaObject::Color livecolor;
  LuaObject::Color deadcolor;

  int gcstate;  /* state of garbage collector */
  int gckind;  /* kind of GC running */
  int gcrunning;  /* true if GC is running */

  LuaObject *allgc;  /* list of all collectable objects */
  LuaObject **sweepgc;  /* current position of sweep */

  LuaList::iterator sweepgc2;

  LuaObject *finobj;  /* list of collectable objects with finalizers */
  LuaList tobefnz;  /* list of userdata to be GC */

  // The global garbage collector.
  LuaCollector gc_;

  LuaUpvalue uvhead;  /* head of double-linked list of all open upvalues */
  Mbuffer buff;  /* temporary buffer for string concatenation */
  int gcpause;  /* size of pause between successive GCs */
  int gcmajorinc;  /* how much to wait for a major GC (only in gen. mode) */
  int gcstepmul;  /* GC `granularity' */

  LuaCallback panic;  /* to be called in unprotected errors */
  LuaThread *mainthread;
  const double *version;  /* pointer to version number */

  LuaString *memerrmsg;  /* memory-error message */
  LuaString *tagmethod_names_[TM_N];  /* array with tag-method names */
  LuaTable *base_metatables_[LUA_NUMTAGS];  /* metatables for basic types */

  int call_depth_;

  LuaAnchor* anchor_head_;
  LuaAnchor* anchor_tail_;

  int instanceCounts[LUA_NUMTAGS];

private:

  size_t totalbytes_;  /* number of bytes currently allocated - GCdebt */
  ptrdiff_t GCdebt_;  /* bytes allocated not yet compensated by the collector */
};


LuaStringTable* getGlobalStringtable();

struct LuaAnchor {

  LuaAnchor() {
    object_ = NULL;
    link(thread_G);
  }

  LuaAnchor(LuaObject* object) {
    object_ = object;
    link(thread_G);
  }

  void operator = (LuaObject* object) {
    object_ = object;
  }

  LuaObject* operator -> () {
    return object_;
  }

  operator bool() const {
    return object_ != NULL;
  }

  ~LuaAnchor() {
    unlink();
  }

  void link(LuaVM* state) {
    state_ = state;

    prev_ = NULL;
    next_ = NULL;

    if(state_->anchor_tail_) {
      state->anchor_tail_->next_ = this;
      prev_ = state->anchor_tail_;
      state->anchor_tail_ = this;
    }
    else {
      state->anchor_head_ = this;
      state->anchor_tail_ = this;
    }
  }

  void unlink() {
    if(state_->anchor_head_ == this) state_->anchor_head_ = next_;
    if(state_->anchor_tail_ == this) state_->anchor_tail_ = prev_;

    if(prev_) prev_->next_ = next_;
    if(next_) next_->prev_ = prev_;

    prev_ = NULL;
    next_ = NULL;
    object_ = NULL;
    state_ = NULL;
  }
   
  LuaObject* object_;
  LuaVM* state_;
  LuaAnchor* prev_;
  LuaAnchor* next_;
};

