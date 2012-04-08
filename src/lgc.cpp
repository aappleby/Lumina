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
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
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
#define stddebt(g)	(-cast(l_mem, g->getTotalBytes()/100) * g->gcpause)


static void markobject (LuaObject *o);

inline void markvalue(TValue* v) {
  if(v->isCollectable()) {
    markobject(v->getObject());
  }
}



/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** tells whether a key or value can be cleared from a weak
** table. Non-collectable objects are never removed from weak
** tables. Strings behave as `values', so are never removed too. for
** other objects: if really collected, cannot keep them; for objects
** being finalized, keep them in keys, but not in values
*/

// Strings behave as value types _when used as table keys_.

static int isWeakTableRef (const TValue *o) {
  if (!o->isCollectable()) {
    return 0;
  }
  
  if (o->isString()) {
    o->getString()->stringmark();  /* strings are `values', so are never weak */
    return 0;
  }
  
  return o->getObject()->isWhite();
}


/*
** barrier that moves collector forward, that is, mark the white object
** being pointed by a black object.
*/
void luaC_barrier (LuaObject *o, TValue value) {
  if(!o->isBlack()) return;
  if(!value.isWhite()) return;
  LuaObject* v = value.getObject();

  global_State *g = thread_G;
  assert(o->isBlack() && v->isWhite() && !v->isDead() && !o->isDead());
  assert(isgenerational(g) || (g->gcstate != GCSpause));
  assert(o->type() != LUA_TTABLE);
  if (keepinvariant(g))  /* must keep invariant? */
    markobject(v);  /* restore invariant */
  else {  /* sweep phase */
    assert(issweepphase(g));
    o->makeLive();  /* mark main obj. as white to avoid other barriers */
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

void luaC_barrierback (LuaObject *o, TValue v) {
  if(!o->isBlack()) return;
  if(!v.isWhite()) return;

  global_State *g = thread_G;

  assert(o->isBlack() && !o->isDead() && o->isTable());
  o->blackToGray();  /* make object gray (again) */

  o->next_gray_ = g->grayagain;
  g->grayagain = o;
}


/*
** barrier for prototypes. When creating first closure (cache is
** NULL), use a forward barrier; this may be the only closure of the
** prototype (if it is a "regular" function, with a single instance)
** and the prototype may be big, so it is better to avoid traversing
** it again. Otherwise, use a backward barrier, to avoid marking all
** possible instances.
*/
void luaC_barrierproto (Proto *p, Closure *c) {
  global_State *g = thread_G;
  if(!p->isBlack()) return;

  if (p->cache == NULL) {  /* first time? */
    luaC_barrier(p, TValue(c));
  }
  else {  /* use a backward barrier */
    p->blackToGray();  /* make prototype gray (again) */
    p->next_gray_ = g->grayagain;
    g->grayagain = p;
  }
}


/*
** check color (and invariants) for an upvalue that was closed,
** i.e., moved into the 'allgc' list
*/
void luaC_checkupvalcolor (global_State *g, UpVal *uv) {
  // open upvalues are never black
  assert(!uv->isBlack());

  if (uv->isGray()) {
    if (keepinvariant(g)) {
      uv->clearOld();  /* see MOVE OLD rule */
      uv->grayToBlack();  /* it is being visited now */
      markvalue(uv->v);
    }
    else {
      assert(issweepphase(g));
      uv->makeLive();
    }
  }
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** mark an object. Userdata and closed upvalues are visited and turned
** black here. Strings remain gray (it is the same as making them
** black). Other objects are marked gray and added to appropriate list
** to be visited (and turned black) later. (Open upvalues are already
** linked in 'headuv' list.)
*/
static void markobject(LuaObject *o) {
  GCVisitor v;
  v.MarkObject(o);
}

void GCVisitor::MarkValue(TValue v) {
  if(v.isCollectable()) {
    MarkObject(v.getObject());
  }
}

void GCVisitor::MarkObject(LuaObject* o) {
  if(o == NULL) return;
  if(o->isGray()) {
    return;
  }
  if(o->isBlack()) {
    return;
  }

  if(!o->isFixed()) {
    assert(o->isLiveColor());
  }

  o->VisitGC(*this);
  return;
}

void GCVisitor::PushGray(LuaObject* o) {
  o->next_gray_  = thread_G->grayhead_;
  thread_G->grayhead_ = o;
}

void GCVisitor::PushGrayAgain(LuaObject* o) {
  o->next_gray_ = thread_G->grayagain;
  thread_G->grayagain = o;
}



/*
** mark root set and reset all gray lists, to start a new
** incremental (or full) collection
*/
static void markroot (global_State *g) {
  g->grayhead_ = NULL;
  g->grayagain = NULL;
  g->weak = NULL;
  g->allweak = NULL;
  g->ephemeron = NULL;

  markobject(g->mainthread);
  markvalue(&g->l_registry);
  
  for (int i=0; i < LUA_NUMTAGS; i++) {
    markobject(g->base_metatables_[i]);
  }

  /* mark any finalizing object left from previous cycle */
  for (LuaObject* o = g->tobefnz; o != NULL; o = o->next) {
    o->makeLive();
    markobject(o);
  }
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/

struct tableTraverseInfo {
  int markedAny;
  int hasclears;
  int propagate;
  int weakkey;
  int weakval;
};

void traverseweakvalue_callback (TValue* key, TValue* val, void* blob) {
  tableTraverseInfo& info = *(tableTraverseInfo*)blob;
  assert(!key->isDeadKey() || val->isNil());

  if (val->isNil()) {
    if (key->isWhite()) {
      key->setDeadKey();  /* unused and unmarked key; remove it */
    }
    return;
  }

  assert(!key->isNil());

  markvalue(key);  // mark key

  // is there a white value? table will have to be cleared
  if (isWeakTableRef(val)) {
    info.hasclears = 1;
  }
}

void traverseephemeronCB(TValue* key, TValue* val, void* blob) {
  tableTraverseInfo& info = *(tableTraverseInfo*)blob;
  assert(!key->isDeadKey() || val->isNil());

  // If the node's value is nil, mark the key as dead.
  if (val->isNil()) {
    if (key->isWhite()) {
      key->setDeadKey();  /* unused and unmarked key; remove it */
    }
    return;
  }

  if (isWeakTableRef(key)) {  /* key is not marked (yet)? */
    info.hasclears = 1;  /* table must be cleared */
   
    if (val->isWhite()) { /* value not marked yet? */
      info.propagate = 1;  /* must propagate again */
    }
    return;
  }

  if (val->isWhite()) {  /* value not marked yet? */
    info.markedAny = 1;
    markobject(val->getObject());  /* mark it now */
  }
}

void traverseStrongNode(TValue* key, TValue* val, void* blob) {
  tableTraverseInfo& info = *(tableTraverseInfo*)blob;
  assert(!key->isDeadKey() || val->isNil());

  if (val->isNil()) {
    if (key->isWhite()) {
      key->setDeadKey();  /* unused and unmarked key; remove it */
    }
    return;
  }

  assert(key->isNotNil());
  markvalue(key);  /* mark key */
  markvalue(val);  /* mark value */
}

static int traverseephemeron (Table *h) {
  tableTraverseInfo info;
  info.markedAny = 0;
  info.hasclears = 0;
  info.propagate = 0;

  h->traverse(traverseephemeronCB, &info);

  if (info.propagate) {
    /* have to propagate again */
    h->next_gray_ = thread_G->ephemeron;
    thread_G->ephemeron = h;
  }
  else if (info.hasclears) {
    /* does table have white keys? */
    /* may have to clean white keys */
    h->next_gray_ = thread_G->allweak;
    thread_G->allweak = h;
  }
  else {
    /* no white keys */
    /* no need to clean */
    h->next_gray_ = thread_G->grayagain;
    thread_G->grayagain = h;
  }
  return info.markedAny;
}


// Table modes really should be a flag on the table instead
// of a special tag method just to get/set two flags...
static int traversetable (global_State *g, Table *h) {
  markobject(h->metatable);

  const TValue *mode = fasttm(h->metatable, TM_MODE);

  tableTraverseInfo info;
  info.markedAny = 0;
  info.hasclears = 0;
  info.propagate = 0;
  info.weakkey = 0;
  info.weakval = 0;

  // Strong keys, strong values - use strong table traversal.
  if(mode == NULL) {
    return h->traverse(traverseStrongNode, &info);
  }

  if(mode) {
    assert(mode->isString());
    info.weakkey = (strchr(mode->getString()->c_str(), 'k') != NULL);
    info.weakval = (strchr(mode->getString()->c_str(), 'v') != NULL);
    assert(info.weakkey || info.weakval);
  }

  // Keep table gray

  // Strong keys, weak values - use weak table traversal.
  if (!info.weakkey) {
    h->blackToGray();
    h->traverse(traverseweakvalue_callback, &info);

    if (info.hasclears) {
      h->next_gray_ = thread_G->weak;
      thread_G->weak = h;
    }
    else {
      /* no white values */
      /* no need to clean */
      h->next_gray_ = thread_G->grayagain;
      thread_G->grayagain = h;
    }
    return TRAVCOST + (int)h->hashtable.size();
  }

  // Weak keys, strong values - use ephemeron traversal.
  if (!info.weakval) {
    h->blackToGray();
    h->traverse(traverseephemeronCB, &info);

    if (info.propagate) {
      h->next_gray_ = thread_G->ephemeron;
      thread_G->ephemeron = h;
    }
    else if (info.hasclears) {
      /* does table have white keys? */
      h->next_gray_ = thread_G->allweak;
      thread_G->allweak = h;
    }
    else {
      /* no white keys */
      /* no need to clean */
      h->next_gray_ = thread_G->grayagain;
      thread_G->grayagain = h;
    }

    return TRAVCOST + (int)h->array.size() + (int)h->hashtable.size();
  }

  // Both keys and values are weak.
  h->blackToGray();
  h->next_gray_ = thread_G->allweak;
  thread_G->allweak = h;
  return TRAVCOST;
}


static int traverseclosure (global_State *g, Closure *cl) {
  if (cl->isC) {
    int i;
    for (i=0; i<cl->nupvalues; i++)  /* mark its upvalues */
      markvalue(&cl->pupvals_[i]);
  }
  else {
    int i;
    assert(cl->nupvalues == cl->proto_->upvalues.size());
    markobject(cl->proto_);  /* mark its prototype */
    for (i=0; i<cl->nupvalues; i++)  /* mark its upvalues */
      markobject(cl->ppupvals_[i]);
  }
  return TRAVCOST + cl->nupvalues;
}

/*
** traverse one gray object, turning it to black (except for threads,
** which are always gray).
** Returns number of values traversed.
*/

// TODO(aappleby): Why do we mark objects black _before_ we traverse their children?

static int propagatemark (global_State *g) {
  // pop gray object off the list
  LuaObject *o = g->grayhead_;
  g->grayhead_ = o->next_gray_;
  assert(o->isGray());

  // traverse its children and add them to the gray list(s)
  if(o->isTable()) {
    o->grayToBlack();
    return traversetable(g, dynamic_cast<Table*>(o));
  }

  if(o->isLClosure() || o->isCClosure()) {
    o->grayToBlack();
    return traverseclosure(g, dynamic_cast<Closure*>(o));
  }

  if(o->isThread()) {
    GCVisitor visitor;
    return o->PropagateGC(visitor);
  }

  if(o->isProto()) {
    GCVisitor visitor;
    return o->PropagateGC(visitor);
  }

  assert(false);
  return 0;
}

/*
** retraverse all gray lists. Because tables may be reinserted in other
** lists when traversed, traverse the original lists to avoid traversing
** twice the same table (which is not wrong, but inefficient)
*/
static void retraversegrays (global_State *g) {
  LuaObject *weak = g->weak;  /* save original lists */
  LuaObject *grayagain = g->grayagain;
  LuaObject *ephemeron = g->ephemeron;
  
  g->weak = NULL;
  g->grayagain = NULL;
  g->ephemeron = NULL;

  while (g->grayhead_) propagatemark(g);
  g->grayhead_ = grayagain;
  while (g->grayhead_) propagatemark(g);
  g->grayhead_ = weak;
  while (g->grayhead_) propagatemark(g);
  g->grayhead_ = ephemeron;
  while (g->grayhead_) propagatemark(g);
}


// TODO(aappleby): what the hell does this do?

static void convergeephemerons (global_State *g) {
  int changed;
  do {
    LuaObject *w;
    LuaObject *next = g->ephemeron;  /* get ephemeron list */
    g->ephemeron = NULL;  /* tables will return to this list when traversed */
    changed = 0;
    while ((w = next) != NULL) {
      next = w->next_gray_;
      if (traverseephemeron(dynamic_cast<Table*>(w))) {  /* traverse marked some value? */
        /* propagate changes */
        while (g->grayhead_) propagatemark(g);
        changed = 1;  /* will have to revisit all ephemeron tables */
      }
    }
  } while (changed);
}

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


/*
** clear entries with unmarked keys from all weaktables in list 'l' up
** to element 'f'
*/
static void clearkeys (LuaObject *l) {
  for (; l != NULL; l = l->next_gray_) {
    Table *h = dynamic_cast<Table*>(l);

    for(int i = 0; i < (int)h->hashtable.size(); i++) {
      Node* n = h->getNode(i);
      if (n->i_val.isNotNil() && (isWeakTableRef(&n->i_key))) {
        n->i_val = TValue::nil;  /* remove value ... */
        if (n->i_key.isWhite()) {
          n->i_key.setDeadKey();  /* unused and unmarked key; remove it */
        }
      }
    }
  }
}


/*
** clear entries with unmarked values from all weaktables in list 'l' up
** to element 'f'
*/
static void clearvalues (LuaObject *l, LuaObject *f) {
  for (; l != f; l = l->next_gray_) {
    Table *h = dynamic_cast<Table*>(l);
    for (int i = 0; i < (int)h->array.size(); i++) {
      TValue *o = &h->array[i];
      if (isWeakTableRef(o)) {  /* value was collected? */
        *o = TValue::nil;  /* remove value */
      }
    }
    for(int i = 0; i < (int)h->hashtable.size(); i++) {
      Node* n = h->getNode(i);
      if (n->i_val.isNotNil() && isWeakTableRef(&n->i_val)) {
        n->i_val = TValue::nil;
        if (n->i_key.isWhite()) {
          n->i_key.setDeadKey();
        }
      }
    }
  }
}

static void freeobj (LuaObject *o) {
  lua_State *L = thread_L;
  if(o->isThread()) {
    luaE_freethread(L, dynamic_cast<lua_State*>(o));
  } else {
    delete o;
  }
}


#define sweepwholelist(p)	sweeplist(p,MAX_LUMEM)
static LuaObject **sweeplist (LuaObject **p, size_t count);


/*
** sweep the (open) upvalues of a thread and resize its stack and
** list of call-info structures.
*/

static void sweepthread (lua_State *L1) {
  if (L1->stack.empty()) return;  /* stack not completely built yet */
  sweeplist(&L1->openupval, MAX_LUMEM);  /* sweep open upvalues */
  {
    THREAD_CHANGE(L1);
    CallInfo *ci = L1->ci_;
    CallInfo *next = ci->next;
    ci->next = NULL;
    while ((ci = next) != NULL) {
      next = ci->next;
      delete ci;
    }
  }
  /* should not change the stack during an emergency gc cycle */
  if (thread_G->gckind != KGC_EMERGENCY) {
    THREAD_CHANGE(L1);
    L1->shrinkstack();
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


static LuaObject** sweepListNormal (LuaObject** p, size_t count) {
  global_State *g = thread_G;
  l_mem debt = g->getGCDebt();

  while (*p != NULL && count-- > 0) {
    LuaObject *curr = *p;
    if (curr->isDead()) {  /* is 'curr' dead? */
      *p = curr->next;  /* remove 'curr' from list */
      freeobj(curr);  /* erase 'curr' */
    }
    else {
      if (curr->isThread()) {
        /* sweep thread's upvalues */
        sweepthread(dynamic_cast<lua_State*>(curr));
      }
      /* update marks */
      curr->makeLive();
      p = &curr->next;  /* go to next element */
    }
  }
  g->setGCDebt(debt);  /* sweeping should not change debt */
  return p;
}

static LuaObject** sweepListGenerational (LuaObject **p, size_t count) {
  global_State *g = thread_G;
  l_mem debt = g->getGCDebt();  /* current debt */
  while (*p != NULL && count-- > 0) {
    LuaObject *curr = *p;
    if (curr->isDead()) {  /* is 'curr' dead? */
      *p = curr->next;  /* remove 'curr' from list */
      freeobj(curr);  /* erase 'curr' */
    }
    else {
      if (curr->isThread()) {
        sweepthread(dynamic_cast<lua_State*>(curr));  /* sweep thread's upvalues */
      }
      if (curr->isOld()) {
        static LuaObject *nullp = NULL;
        p = &nullp;  /* stop sweeping this list */
        break;
      }
      /* update marks */
      curr->setOld();
      p = &curr->next;  /* go to next element */
    }
  }
  g->setGCDebt(debt);  /* sweeping should not change debt */
  return p;
}

static LuaObject** sweeplist (LuaObject **p, size_t count) {
  if(isgenerational(thread_G)) {
    return sweepListGenerational(p,count);
  } else {
    return sweepListNormal(p,count);
  }
}

void deletelist (LuaObject*& head) {
  l_mem debt = thread_G->getGCDebt();

  while (head != NULL) {
    LuaObject *curr = head;
    head = curr->next;
    freeobj(curr);
  }

  thread_G->setGCDebt(debt);  /* sweeping should not change debt */
}

/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

static void checkSizes () {
  global_State *g = thread_G;
  if (g->gckind != KGC_EMERGENCY) {  /* do not change sizes in emergency */
    int hs = g->strings_->size_ / 2;  /* half the size of the string table */
    if (g->strings_->nuse_ < cast(uint32_t, hs))  /* using less than that half? */
      luaS_resize(hs);  /* halve its size */
    g->buff.buffer.clear();
  }
}

static LuaObject *udata2finalize (global_State *g) {
  LuaObject *o = g->tobefnz;  /* get first element */
  assert(o->isFinalized());
  g->tobefnz = o->next;  /* remove it from 'tobefnz' list */
  o->next = g->allgc;  /* return it to 'allgc' list */
  g->allgc = o;
  /* mark that it is not in 'tobefnz' */
  o->clearSeparated();
  assert(!o->isOld());  /* see MOVE OLD rule */
  if (!keepinvariant(g))  /* not keeping invariant? */
    o->makeLive();  /* "sweep" object */
  return o;
}


static void dothecall (lua_State *L, void *ud) {
  THREAD_CHECK(L);
  UNUSED(ud);
  luaD_call(L, L->top - 2, 0, 0);
}


static void GCTM (int propagateerrors) {
  lua_State* L = thread_L;
  global_State *g = thread_G;
  TValue v = TValue(udata2finalize(g));
  const TValue *tm = luaT_gettmbyobj(&v, TM_GC);
  if(tm == NULL) return;
  if(!tm->isFunction()) return;

  int status;
  uint8_t oldah = L->allowhook;
  int running  = g->gcrunning;
  L->allowhook = 0;  // stop debug hooks during GC metamethod
  g->gcrunning = 0;  // avoid GC steps
  L->top[0] = *tm;  // push finalizer...
  L->top[1] = v; // ... and its argument
  L->top += 2;  // and (next line) call the finalizer
  status = luaD_pcall(L, dothecall, NULL, savestack(L, L->top - 2), 0);
  L->allowhook = oldah;  // restore hooks
  g->gcrunning = running;  // restore state

  if (status != LUA_OK && propagateerrors) {  // error while running __gc?
    if (status == LUA_ERRRUN) {  // is there an error msg.?
      luaO_pushfstring(L, "error in __gc metamethod (%s)", lua_tostring(L, -1));
      status = LUA_ERRGCMM;  // error in __gc metamethod
    }
    luaD_throw(status);  // re-send error
  }
}


/*
** move all unreachable objects (or 'all' objects) that need
** finalization from list 'finobj' to list 'tobefnz' (to be finalized)
*/
static void separatetobefnz (int all) {
  global_State *g = thread_G;
  LuaObject **p = &g->finobj;
  LuaObject *curr;
  LuaObject **lastnext = &g->tobefnz;
  /* find last 'next' field in 'tobefnz' list (to add elements in its end) */
  while (*lastnext != NULL)
    lastnext = &(*lastnext)->next;
  while ((curr = *p) != NULL) {  /* traverse all finalizable objects */
    assert(!curr->isFinalized());
    assert(curr->isSeparated());
    if (!(all || curr->isWhite()))  /* not being collected? */
      p = &curr->next;  /* don't bother with it */
    else {
      /* won't be finalized again */
      curr->setFinalized();
      *p = curr->next;  /* remove 'curr' from 'finobj' list */
      curr->next = *lastnext;  /* link at the end of 'tobefnz' list */
      *lastnext = curr;
      lastnext = &curr->next;
    }
  }
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/
void luaC_checkfinalizer (LuaObject *o, Table *mt) {
  global_State *g = thread_G;
  if (o->isSeparated() || /* obj. is already separated... */
      o->isFinalized() ||                           /* ... or is finalized... */
      fasttm(mt, TM_GC) == NULL)                /* or has no finalizer? */
    return;  /* nothing to be done */
  else {  /* move 'o' to 'finobj' list */
    // Removing a node in the middle of a singly-linked list requires
    // a scan of the list, lol.
    LuaObject **p;
    for (p = &g->allgc; *p != o; p = &(*p)->next) ;
    *p = o->next;  /* remove 'o' from root list */
    o->next = g->finobj;  /* link it in list 'finobj' */
    g->finobj = o;
    /* mark it as such */
    o->setSeparated();
    o->clearOld();  /* see MOVE OLD rule */
  }
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
void luaC_changemode (lua_State *L, int mode) {
  THREAD_CHECK(L);
  global_State *g = G(L);
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
    g->sweepstrgc = 0;
    g->gcstate = GCSsweepstring;
    g->gckind = KGC_NORMAL;
    luaC_runtilstate(~sweepphases);
  }
}


/*
** call all pending finalizers
*/
static void callallpendingfinalizers (int propagateerrors) {
  global_State *g = thread_G;
  while (g->tobefnz) {
    g->tobefnz->clearOld();
    GCTM(propagateerrors);
  }
}


void luaC_freeallobjects () {
  // separate all objects with finalizers
  separatetobefnz(1);
  assert(thread_G->finobj == NULL);

  // finalize everything
  callallpendingfinalizers(0);

  // finalizers can create objs. in 'finobj'
  deletelist(thread_G->finobj);
  deletelist(thread_G->allgc);

  // free all string lists
  for (int i = 0; i < thread_G->strings_->size_; i++) {
    deletelist(thread_G->strings_->hash_[i]);
  }

  assert(thread_G->strings_->nuse_ == 0);
}


static void atomic () {
  global_State *g = thread_G;
  LuaObject *origweak, *origall;
  assert(!g->mainthread->isWhite());
  markobject(thread_L);  /* mark running thread */
  /* registry and global metatables may be changed by API */
  markvalue(&g->l_registry);
  
  /* mark basic metatables */
  for (int i=0; i < LUA_NUMTAGS; i++) {
    markobject(g->base_metatables_[i]);
  }

  // remark occasional upvalues of (maybe) dead threads
  // mark all values stored in marked open upvalues. (See comment in 'lstate.h'.)
  for (UpVal* uv = g->uvhead.unext; uv != &g->uvhead; uv = uv->unext) {
    if (uv->isGray()) {
      markvalue(uv->v);
    }
  }

  /* traverse objects caught by write barrier and by 'remarkupvals' */
  retraversegrays(g);
  convergeephemerons(g);
  /* at this point, all strongly accessible objects are marked. */
  /* clear values from weak tables, before checking finalizers */
  clearvalues(g->weak, NULL);
  clearvalues(g->allweak, NULL);
  origweak = g->weak; origall = g->allweak;
  separatetobefnz(0);  /* separate objects to be finalized */
  
  /* mark userdata that will be finalized */
  for (LuaObject* o = g->tobefnz; o != NULL; o = o->next) {
    o->makeLive();
    markobject(o);
  }
  
  /* remark, to propagate `preserveness' */
  while (g->grayhead_) propagatemark(g);
  convergeephemerons(g);
  /* at this point, all resurrected objects are marked. */
  /* remove dead objects from weak tables */
  clearkeys(g->ephemeron);  /* clear keys from all ephemeron tables */
  clearkeys(g->allweak);  /* clear keys from all allweak tables */
  /* clear values from resurrected weak tables */
  clearvalues(g->weak, origweak);
  clearvalues(g->allweak, origall);
  g->sweepstrgc = 0;  /* prepare to sweep strings */
  g->gcstate = GCSsweepstring;
  
  std::swap(g->livecolor, g->deadcolor);
}


static l_mem singlestep () {
  global_State *g = thread_G;
  switch (g->gcstate) {
    case GCSpause: {
      if (!isgenerational(g)) markroot(g);  /* start a new collection */
      /* in any case, root must be marked */
      assert(!g->mainthread->isWhite() && !g->l_registry.getObject()->isWhite());
      g->gcstate = GCSpropagate;
      return GCROOTCOST;
    }
    case GCSpropagate: {
      if (g->grayhead_)
        return propagatemark(g);
      else {  /* no more `gray' objects */
        g->gcstate = GCSatomic;  /* finish mark phase */
        atomic();
        return GCATOMICCOST;
      }
    }
    case GCSsweepstring: {
      if (g->sweepstrgc < g->strings_->size_) {
        sweeplist(&g->strings_->hash_[g->sweepstrgc++], MAX_LUMEM);
        return GCSWEEPCOST;
      }
      else {  /* no more strings to sweep */
        g->sweepgc = &g->finobj;  /* prepare to sweep finalizable objects */
        g->gcstate = GCSsweepudata;
        return 0;
      }
    }
    case GCSsweepudata: {
      if (*g->sweepgc) {
        g->sweepgc = sweeplist(g->sweepgc, GCSWEEPMAX);
        return GCSWEEPMAX*GCSWEEPCOST;
      }
      else {
        g->sweepgc = &g->allgc;  /* go to next phase */
        g->gcstate = GCSsweep;
        return GCSWEEPCOST;
      }
    }
    case GCSsweep: {
      if (*g->sweepgc) {
        g->sweepgc = sweeplist(g->sweepgc, GCSWEEPMAX);
        return GCSWEEPMAX*GCSWEEPCOST;
      }
      else {
        /* sweep main thread */
        LuaObject *mt = g->mainthread;
        sweeplist(&mt, 1);
        checkSizes();
        g->gcstate = GCSpause;  /* finish collection */
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
  global_State *g = thread_G;
  while (!(statesmask & (1 << g->gcstate)))
    singlestep();
}


static void generationalcollection () {
  global_State *g = thread_G;
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
  global_State *g = thread_G;
  l_mem lim = g->gcstepmul;  /* how much to work */
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
  global_State *g = thread_G;
  int i;
  if (isgenerational(g)) generationalcollection();
  else step();
  for (i = 0; i < GCFINALIZENUM && g->tobefnz; i++) {
    GCTM(1);  /* Call a few pending finalizers */
  }
}


void luaC_checkGC() {
  if(thread_G->getGCDebt() > 0) {
    luaC_step();
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
  lua_State *L = thread_L;
  global_State *g = G(L);
  int origkind = g->gckind;
  assert(origkind != KGC_EMERGENCY);
  if (!isemergency)   /* do not run finalizers during emergency GC */
    callallpendingfinalizers(1);
  if (keepinvariant(g)) {  /* marking phase? */
    /* must sweep all objects to turn them back to white
       (as white has not changed, nothing will be collected) */
    g->sweepstrgc = 0;
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
  if (!isemergency)   /* do not run finalizers during emergency GC */
    callallpendingfinalizers(1);
}

/* }====================================================== */


