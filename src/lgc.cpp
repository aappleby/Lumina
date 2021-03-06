/*
** $Id: lgc.c,v 2.116 2011/12/02 13:18:41 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltm.h"

#define bitmask(b)		(1<<(b))

/* how much to allocate before next GC step */
#define GCSTEPSIZE	1024

/* maximum number of elements to sweep in each single step */
#define GCSWEEPMAX	40

/* cost of sweeping one element */
#define GCSWEEPCOST	1

/* maximum number of finalizers to call in each GC step */
#define GCFINALIZENUM	4

/* cost of marking the root set */
#define GCROOTCOST	10

/* cost of atomic step */
#define GCATOMICCOST	1000

/*
** standard negative debt for GC; a reasonable "time" to wait before
** starting a new cycle
*/
#define stddebt(g)	(-cast(ptrdiff_t, g->getTotalBytes()/100) * g->gcpause)


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** barrier that moves collector forward, that is, mark the white object
** being pointed by a black object.
*/

// TODO(aappleby): If this is replaced with barrierback everything still works.
// Is it needed?

void luaC_barrier (LuaObject *o, LuaValue value) {
  if(!o->isBlack()) return;
  if(!value.isWhite()) return;

  LuaObject* v = value.getObject();

  LuaVM *g = thread_G;
  assert(o->isBlack() && v->isWhite() && !v->isDead() && !o->isDead());
  assert(isgenerational(g) || (g->gcstate != GCSpause));
  assert(o->type() != LUA_TTABLE);

  if (keepinvariant(g)) {  // must keep invariant?
    LuaGCVisitor visitor(&g->gc_);
    visitor.MarkObject(v);  // restore invariant
  }
  else {  // sweep phase
    assert(issweepphase(g));
    o->makeLive();  // mark main obj. as white to avoid other barriers
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again. (Current implementation
** only works for tables; access to 'gclist' is not uniform across
** different types.)
*/

// If we add a white object to a black table, we need to force the garbage
// collector to re-traverse the table by putting it back on a gray list.

void luaC_barrierback (LuaObject *o, LuaValue v) {
  if(!o->isBlack()) return;
  if(!v.isWhite()) return;

  assert(o->isTable());

  thread_G->gc_.grayagain_.Push(o);
}


/*
** barrier for prototypes. When creating first closure (cache is
** NULL), use a forward barrier; this may be the only closure of the
** prototype (if it is a "regular" function, with a single instance)
** and the prototype may be big, so it is better to avoid traversing
** it again. Otherwise, use a backward barrier, to avoid marking all
** possible instances.
*/

// TODO(aappleby): If this just does the grayagain push everything works,
// is it needed?

void luaC_barrierproto (LuaProto *p, LuaClosure* c) {
  if(!p->isBlack()) return;

  if (p->cache == NULL) {  // first time?
    luaC_barrier(p, LuaValue(c));
  }
  else {  // use a backward barrier
    thread_G->gc_.grayagain_.Push(p);
  }
}


/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** mark root set and reset all gray lists, to start a new
** incremental (or full) collection
*/
static void markroot (LuaVM *g) {
  g->gc_.ClearGraylists();

  LuaGCVisitor visitor(&g->gc_);

  visitor.MarkObject(g->mainthread);
  visitor.MarkValue(g->l_registry);
  
  for (int i=0; i < LUA_NUMTAGS; i++) {
    visitor.MarkObject(g->base_metatables_[i]);
  }

  /* mark any finalizing object left from previous cycle */
  for(LuaList::iterator it = g->tobefnz.begin(); it; ++it) {
    it->makeLive();
    visitor.MarkObject(it);
  }

  // Mark all objects anchored in the C stack
  for(LuaAnchor* cursor = g->anchor_head_; cursor; cursor = cursor->next_) {
    if(cursor->object_) {
      visitor.MarkObject(cursor->object_);
    }
  }
}

/*
** {======================================================
** Sweep Functions
** =======================================================
*/


static bool sweeplist (LuaList::iterator& it, size_t count);


/*
** sweep the (open) upvalues of a thread and resize its stack and
** list of call-info structures.
*/

static void sweepthread (LuaThread *L1) {
  LuaList::iterator it = L1->stack_.open_upvals_.begin();
  sweeplist(it, MAX_LUMEM);  /* sweep open upvalues */

  L1->stack_.sweepCallinfo();

  /* should not change the stack during an emergency gc cycle */
  if (thread_G->gckind != KGC_EMERGENCY) {
    THREAD_CHANGE(L1);
    L1->stack_.shrink();
  }
}


/*
** sweep at most 'count' elements from a list of GCObjects erasing dead
** objects, where a dead (not alive) object is one marked with the "old"
** (non current) white and not fixed.
** In non-generational mode, change all non-dead objects back to white,
** preparing for next collection cycle.
** In generational mode, keep black objects black, and also mark them as
** old; stop when hitting an old object, as all objects after that
** one will be old too.
** When object is a thread, sweep its list of open upvalues too.
*/


bool sweepListNormal2 (LuaList::iterator& it, size_t count) {
  while(it) {
    if(count-- <= 0) return false;

    if (it->isDead()) {  /* is 'curr' dead? */
      LuaObject* dead = it;
      it.pop();
      delete dead;
    }
    else {
      if (it->isThread()) {
        /* sweep thread's upvalues */
        LuaObject* o = it;
        sweepthread(dynamic_cast<LuaThread*>(o));
      }
      /* update marks */
      it->makeLive();
      ++it;
    }
  }
  return true;
}

bool sweepListGenerational2 (LuaList::iterator& it, size_t count) {
  while(it) {
    if(count-- <= 0) return false;

    if (it->isDead()) {  /* is 'curr' dead? */
      LuaObject* dead = it;
      it.pop();
      delete dead;
    }
    else {
      if (it->isThread()) {
        sweepthread(dynamic_cast<LuaThread*>(it.get()));  /* sweep thread's upvalues */
      }
      if (it->isOld()) {
        return true;
      }
      /* update marks */
      it->setOld();
      ++it;
    }
  }

  return true;
}

bool sweeplist (LuaList::iterator& it, size_t count) {
  if(isgenerational(thread_G)) {
    return sweepListGenerational2(it, count);
  } else {
    return sweepListNormal2(it, count);
  }
}

void deletelist (LuaList& list) {
  while(!list.isEmpty()) {
    LuaObject* o = list.Pop();
    delete o;
  }
}

/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

static void runOneFinalizer (int propagateerrors) {
  LuaThread* L = thread_L;
  LuaVM *g = thread_G;

  LuaObject* o = g->tobefnz.Pop();  /* get first element */
  assert(o->isFinalized());

  /* return it to 'allgc' list */
  o->linkGC(g->allgc);

  /* mark that it is not in 'tobefnz' */
  o->clearSeparated();
  assert(!o->isOld());  /* see MOVE OLD rule */

  if (!keepinvariant(g)) {  /* not keeping invariant? */
    o->makeLive();  /* "sweep" object */
  }

  // Get the finalizer from it.
  LuaValue tm = luaT_gettmbyobj2(o, TM_GC);
  if(!tm.isFunction()) return;

  // Call the finalizer (with a bit of difficulty)
  int gcrunning  = g->gcrunning;
  g->gcrunning = 0;  // avoid GC steps

  // Save the parts of the execution state that will get modified by the call
  LuaExecutionState s = L->saveState(L->stack_.top_);

  L->allowhook = 0;  // stop debug hooks during GC metamethod
  L->errfunc = 0;

  LuaResult status = LUA_OK;
  try {
    L->stack_.top_[0] = tm;  // push finalizer...
    L->stack_.top_[1] = o; // ... and its argument
    L->stack_.top_ += 2;  // and (next line) call the finalizer

    luaD_call(L, 1, 0, 0);
  }
  catch(LuaResult error) {
    status = error;
  }

  L->restoreState(s, status, 0);

  g->gcrunning = gcrunning;  // restore state

  // Report errors during finalization.
  if (status != LUA_OK && propagateerrors) {  // error while running __gc?
    if (status == LUA_ERRRUN) {  // is there an error msg.?
      luaO_pushfstring(L, "error in __gc metamethod (%s)", lua_tostring(L, -1));
      status = LUA_ERRGCMM;  // error in __gc metamethod
    }
    throwError(status);  // re-send error
  }
}


/*
** move all unreachable objects (or 'all' objects) that need
** finalization from list 'finobj' to list 'tobefnz' (to be finalized)
*/
void separatetobefnz (int all) {
  LuaVM *g = thread_G;

  for(LuaList::iterator it = g->finobj.begin(); it;) {
    assert(!it->isFinalized());
    assert(it->isSeparated());

    // not being collected? don't bother with it
    if (!(all || it->isWhite())) {
      ++it;
      continue;
    }

    // won't be finalized again
    LuaObject* o = it;
    it.pop();
    o->setFinalized();
    g->tobefnz.PushTail(o);
  }
}

/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/
void luaC_checkfinalizer (LuaObject *o, LuaTable *mt) {
  LuaVM *g = thread_G;

  // If the object is already separated, is already finalized, or has no
  // finalizer, skip it.
  if(o->isSeparated()) return;
  if(o->isFinalized()) return;
  
  LuaValue tm = fasttm2(mt, TM_GC);
  if(tm.isNone() || tm.isNil()) return;

  // Remove the object from the global GC list and add it to the 'finobj' list.
  o->unlinkGC(g->allgc);
  
  //o->next_ = g->finobj;
  //g->finobj = o;
  g->finobj.Push(o);

  // Mark it as separated, and clear old (MOVE OLD rule).
  o->setSeparated();
  o->clearOld();
}

/* }====================================================== */


/*
** {======================================================
** GC control
** =======================================================
*/


#define sweepphases (bitmask(GCSsweepstring) | bitmask(GCSsweepudata) | bitmask(GCSsweep))

/*
** change GC mode
*/
void luaC_changemode (LuaThread *L, int mode) {
  THREAD_CHECK(L);
  LuaVM *g = G(L);
  if (mode == g->gckind) return;  /* nothing to change */
  if (mode == KGC_GEN) {  /* change to generational mode */
    /* make sure gray lists are consistent */
    luaC_runtilstate(bitmask(GCSpropagate));
    g->lastmajormem = g->getTotalBytes();
    g->gckind = KGC_GEN;
  }
  else {  /* change to incremental mode */
    /* sweep all objects to turn them back to white
       (as white has not changed, nothing extra will be collected) */
    g->strings_->RestartSweep();
    g->gcstate = GCSsweepstring;
    g->gckind = KGC_NORMAL;
    luaC_runtilstate(~sweepphases);
  }
}


/*
** call all pending finalizers
*/
static void callallpendingfinalizers (int propagateerrors) {
  LuaVM *g = thread_G;
  while (!g->tobefnz.isEmpty()) {
    g->tobefnz.begin()->clearOld();
    runOneFinalizer(propagateerrors);
  }
}


void luaC_freeallobjects () {
  // separate all objects with finalizers
  separatetobefnz(1);
  assert(thread_G->finobj.isEmpty());
  //assert(thread_G->finobj == NULL);

  // finalize everything
  callallpendingfinalizers(0);

  // finalizers can create objs. in 'finobj'
  deletelist(thread_G->finobj);
  deletelist(thread_G->allgc);

  // free all string lists
  thread_G->strings_->Clear();
}

void traverseAllObjects() {
  //LuaVM* g = thread_G;
  
  // strings

  // allgc

  // finobj

  // mainthread
}

// We have marked our root objects and incrementally propagated those marks
// out through all objects in the universe.

static void atomic () {
  LuaVM *g = thread_G;

  LuaGCVisitor visitor(&g->gc_);

  assert(!g->mainthread->isWhite());

  /* mark running thread */
  visitor.MarkObject(thread_L);  

  /* registry and global metatables may be changed by API */
  visitor.MarkValue(g->l_registry);
  
  /* mark basic metatables */
  for (int i=0; i < LUA_NUMTAGS; i++) {
    visitor.MarkObject(g->base_metatables_[i]);
  }

  // remark occasional upvalues of (maybe) dead threads
  // mark all values stored in marked open upvalues. (See comment in 'lstate.h'.)
  for (LuaUpvalue* uv = g->uvhead.unext; uv != &g->uvhead; uv = uv->unext) {
    if (uv->isGray()) {
      visitor.MarkValue(*uv->v);
    }
  }

  /* traverse objects caught by write barrier and by 'remarkupvals' */
  g->gc_.RetraverseGrays();
  g->gc_.ConvergeEphemerons();

  /* at this point, all strongly accessible objects are marked. */
  /* clear values from weak tables, before checking finalizers */
  g->gc_.weak_.SweepValues();
  g->gc_.allweak_.SweepValues();

  // Userdata that requires finalization has to be separated from the main gc list
  // and kept alive until the finalizers are called.
  separatetobefnz(0);

  for(LuaList::iterator it = g->tobefnz.begin(); it; ++it) {
    it->makeLive();
    visitor.MarkObject(it);
  }
  
  /* remark, to propagate `preserveness' */
  LuaGCVisitor v(&g->gc_);
  g->gc_.grayhead_.PropagateGC(v);
  g->gc_.ConvergeEphemerons();

  /* at this point, all resurrected objects are marked. */
  /* remove dead objects from weak tables */
  /* clear keys from all ephemeron tables */
  g->gc_.ephemeron_.SweepKeys();

  // clear keys from all allweak tables

  /* clear values from resurrected weak tables */
  g->gc_.weak_.SweepValues();
  g->gc_.allweak_.Sweep();

  g->strings_->RestartSweep();  /* prepare to sweep strings */
  g->gcstate = GCSsweepstring;
  
  std::swap(g->livecolor, g->deadcolor);
}


static ptrdiff_t singlestep () {
  LuaVM *g = thread_G;
  switch (g->gcstate) {
    case GCSpause: {
      if (!isgenerational(g)) {
        // start a new collection
        markroot(g);
      }
      // in any case, root must be marked
      assert(!g->mainthread->isWhite());
      assert(!g->l_registry.isWhite());

      g->gcstate = GCSpropagate;
      return GCROOTCOST;
    }
    case GCSpropagate: {
      if (g->gc_.hasGrays()) {
        LuaGCVisitor visitor(&g->gc_);
        LuaObject *o = g->gc_.grayhead_.Pop();
        return o->PropagateGC(visitor);
      }
      else {  /* no more `gray' objects */
        g->gcstate = GCSatomic;  /* finish mark phase */
        atomic();
        return GCATOMICCOST;
      }
    }
    case GCSsweepstring: {
      bool done = g->strings_->Sweep(isgenerational(g));

      if(!done) {
        return GCSWEEPCOST;
      }
      else {
        // prepare to sweep finalizable objects
        g->sweepgc2 = g->finobj.begin();
        g->gcstate = GCSsweepudata;
        return 0;
      }
    }
    case GCSsweepudata: {
      bool done = sweeplist(g->sweepgc2, GCSWEEPMAX);
      if (!done) {
        return GCSWEEPMAX*GCSWEEPCOST;
      }
      else {
        g->sweepgc2 = g->allgc.begin();
        g->gcstate = GCSsweep;
        return GCSWEEPCOST;
      }
    }
    case GCSsweep: {
      bool done = sweeplist(g->sweepgc2, GCSWEEPMAX);

      if (!done) {
        return GCSWEEPMAX*GCSWEEPCOST;
      }
      else {
        // Last but not least, sweep the main thread
        sweepthread(g->mainthread);
        
        // We have swept everything. If this is not an emergency, try and
        // save some RAM by reducing the size of our internal buffers.
        if (g->gckind != KGC_EMERGENCY) {
          g->strings_->Shrink();
          g->buff.clear();
        }
        
        g->gcstate = GCSpause;
        return GCSWEEPCOST;
      }
    }
    default: assert(0); return 0;
  }
}


/*
** advances the garbage collector until it reaches a state allowed
** by 'statemask'
*/
void luaC_runtilstate (int statesmask) {
  LuaVM *g = thread_G;
  while (!(statesmask & (1 << g->gcstate)))
    singlestep();
}


static void generationalcollection () {
  LuaVM *g = thread_G;
  if (g->lastmajormem == 0) {  /* signal for another major collection? */
    luaC_fullgc(0);  /* perform a full regular collection */
    g->lastmajormem = g->getTotalBytes();  /* update control */
  }
  else {
    luaC_runtilstate(~bitmask(GCSpause));  /* run complete cycle */
    luaC_runtilstate(bitmask(GCSpause));
    if (g->getTotalBytes() > g->lastmajormem/100 * g->gcmajorinc)
      g->lastmajormem = 0;  /* signal for a major collection */
  }
  g->setGCDebt(stddebt(g));
}


static void step () {
  LuaVM *g = thread_G;
  ptrdiff_t lim = g->gcstepmul;  /* how much to work */
  do {  /* always perform at least one single step */
    lim -= singlestep();
  } while (lim > 0 && g->gcstate != GCSpause);
  if (g->gcstate != GCSpause)
    g->setGCDebt(g->getGCDebt() - GCSTEPSIZE);
  else
    g->setGCDebt(stddebt(g));
}


/*
** performs a basic GC step even if the collector is stopped
*/
void luaC_forcestep () {
  LuaVM *g = thread_G;
  int i;
  if (isgenerational(g)) {
    generationalcollection();
  }
  else {
    step();
  }

  // Call a few pending finalizers
  for (i = 0; i < GCFINALIZENUM && (!g->tobefnz.isEmpty()); i++) {
    runOneFinalizer(1);
  }
}

/*
** performs a basic GC step only if collector is running
*/
void luaC_step () {
  if (thread_G->gcrunning) luaC_forcestep();
}


/*
** performs a full GC cycle; if "isemergency", does not call
** finalizers (which could change stack positions)
*/
void luaC_fullgc (int isemergency) {
  LuaThread *L = thread_L;
  LuaVM *g = G(L);
  int origkind = g->gckind;
  assert(origkind != KGC_EMERGENCY);
  
  // do not run finalizers during emergency GC
  if (!isemergency) {   
    callallpendingfinalizers(1);
  }

  if (keepinvariant(g)) {  /* marking phase? */
    /* must sweep all objects to turn them back to white
       (as white has not changed, nothing will be collected) */
    g->strings_->RestartSweep();
    g->gcstate = GCSsweepstring;
  }
  g->gckind = isemergency ? KGC_EMERGENCY : KGC_NORMAL;

  /* finish any pending sweep phase to start a new cycle */
  luaC_runtilstate(bitmask(GCSpause));

  /* run entire collector */
  luaC_runtilstate(~bitmask(GCSpause));
  luaC_runtilstate(bitmask(GCSpause));

  if (origkind == KGC_GEN) {  /* generational mode? */
    /* generational mode must always start in propagate phase */
    luaC_runtilstate(bitmask(GCSpropagate));
  }

  g->gckind = origkind;
  g->setGCDebt(stddebt(g));

  // do not run finalizers during emergency GC
  if (!isemergency) {
    callallpendingfinalizers(1);
  }
}

/* }====================================================== */


