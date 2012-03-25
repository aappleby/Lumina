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

/* basic cost to traverse one object (to be added to the links the
   object may have) */
#define TRAVCOST	5


/*
** standard negative debt for GC; a reasonable "time" to wait before
** starting a new cycle
*/
#define stddebt(g)	(-cast(l_mem, gettotalbytes(g)/100) * g->gcpause)


/*
** 'makewhite' erases all color bits plus the old bit and then
** sets only the current white bit
*/
#define maskcolors	(~(bit2mask(BLACKBIT, OLDBIT) | WHITEBITS))
#define makewhite(g,x)	\
 (gch(x)->marked = cast_byte((gch(x)->marked & maskcolors) | luaC_white(g)))

#define white2gray(x)	resetbits(gch(x)->marked, WHITEBITS)
#define black2gray(x)	resetbit(gch(x)->marked, BLACKBIT)

#define stringmark(s)	((void)((s) && resetbits((s)->marked, WHITEBITS)))


#define isfinalized(x)		testbit(gch(x)->marked, FINALIZEDBIT)

#define checkdeadkey(n)	assert(!ttisdeadkey(&n->i_key) || ttisnil(&n->i_val))


#define checkconsistency(obj) assert(!iscollectable(obj) || righttt(obj))


#define markvalue(g,o) { checkconsistency(o); if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

#define markobject(g,t) { if ((t) && iswhite(obj2gco(t))) reallymarkobject(g, obj2gco(t)); }

static void reallymarkobject (global_State *g, LuaObject *o);


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** link table 'h' into list pointed by 'p'
*/
#define linktable(h,p)	((h)->graylist = *(p), *(p) = obj2gco(h))



// The key for this node is about to be garbage collected. Mark the
// keyvalue as dead so we don't try and dereference it later.
static void removeentry (Node *n) {
  assert(ttisnil(&n->i_val));
  if (valiswhite(&n->i_key))
    setdeadvalue(&n->i_key);  /* unused and unmarked key; remove it */
}


/*
** tells whether a key or value can be cleared from a weak
** table. Non-collectable objects are never removed from weak
** tables. Strings behave as `values', so are never removed too. for
** other objects: if really collected, cannot keep them; for objects
** being finalized, keep them in keys, but not in values
*/
static int iscleared (const TValue *o) {
  if (!iscollectable(o)) return 0;
  else if (ttisstring(o)) {
    stringmark(tsvalue(o));  /* strings are `values', so are never weak */
    return 0;
  }
  else return iswhite(gcvalue(o));
}


/*
** barrier that moves collector forward, that is, mark the white object
** being pointed by a black object.
*/
void luaC_barrier_ (LuaObject *o, LuaObject *v) {
  global_State *g = thread_G;
  assert(isblack(o) && iswhite(v) && !isdead(v) && !isdead(o));
  assert(isgenerational(g) || g->gcstate != GCSpause);
  assert(gch(o)->tt != LUA_TTABLE);
  if (keepinvariant(g))  /* must keep invariant? */
    reallymarkobject(g, v);  /* restore invariant */
  else {  /* sweep phase */
    assert(issweepphase(g));
    makewhite(g, o);  /* mark main obj. as white to avoid other barriers */
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again. (Current implementation
** only works for tables; access to 'gclist' is not uniform across
** different types.)
*/
void luaC_barrierback_ (LuaObject *o) {
  global_State *g = thread_G;
  assert(isblack(o) && !isdead(o) && gch(o)->tt == LUA_TTABLE);
  black2gray(o);  /* make object gray (again) */
  gco2t(o)->graylist = g->grayagain;
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
void luaC_barrierproto_ (Proto *p, Closure *c) {
  global_State *g = thread_G;
  assert(isblack(obj2gco(p)));
  if (p->cache == NULL) {  /* first time? */
    luaC_objbarrier(L, p, c);
  }
  else {  /* use a backward barrier */
    black2gray(obj2gco(p));  /* make prototype gray (again) */
    p->gclist = g->grayagain;
    g->grayagain = obj2gco(p);
  }
}


/*
** check color (and invariants) for an upvalue that was closed,
** i.e., moved into the 'allgc' list
*/
void luaC_checkupvalcolor (global_State *g, UpVal *uv) {
  LuaObject *o = obj2gco(uv);
  assert(!isblack(o));  /* open upvalues are never black */
  if (isgray(o)) {
    if (keepinvariant(g)) {
      resetoldbit(o);  /* see MOVE OLD rule */
      gray2black(o);  /* it is being visited now */
      markvalue(g, uv->v);
    }
    else {
      assert(issweepphase(g));
      makewhite(g, o);
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
static void reallymarkobject (global_State *g, LuaObject *o) {
  assert(iswhite(o) && !isdead(o));
  white2gray(o);
  switch (gch(o)->tt) {
    case LUA_TSTRING: {
      return;  /* for strings, gray is as good as black */
    }
    case LUA_TUSERDATA: {
      Table *mt = gco2u(o)->metatable_;
      markobject(g, mt);
      markobject(g, gco2u(o)->env_);
      gray2black(o);  /* all pointers marked */
      return;
    }
    case LUA_TUPVAL: {
      UpVal *uv = gco2uv(o);
      markvalue(g, uv->v);
      if (uv->v == &uv->value)  /* closed? (open upvalues remain gray) */
        gray2black(o);  /* make it black */
      return;
    }
    case LUA_TFUNCTION: {
      gco2cl(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    case LUA_TTABLE: {
      linktable(gco2t(o), &g->gray);
      break;
    }
    case LUA_TTHREAD: {
      gco2th(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    case LUA_TPROTO: {
      gco2p(o)->gclist = g->gray;
      g->gray = o;
      break;
    }
    default: assert(0);
  }
}


/*
** mark metamethods for basic types
*/
static void markmt (global_State *g) {
  int i;
  for (i=0; i < LUA_NUMTAGS; i++)
    markobject(g, g->mt[i]);
}


/*
** mark all objects in list of being-finalized
*/
static void markbeingfnz (global_State *g) {
  LuaObject *o;
  for (o = g->tobefnz; o != NULL; o = gch(o)->next) {
    makewhite(g, o);
    reallymarkobject(g, o);
  }
}


/*
** mark all values stored in marked open upvalues. (See comment in
** 'lstate.h'.)
*/
static void remarkupvals (global_State *g) {
  UpVal *uv;
  for (uv = g->uvhead.unext; uv != &g->uvhead; uv = uv->unext) {
    if (isgray(obj2gco(uv)))
      markvalue(g, uv->v);
  }
}


/*
** mark root set and reset all gray lists, to start a new
** incremental (or full) collection
*/
static void markroot (global_State *g) {
  g->gray = g->grayagain = NULL;
  g->weak = g->allweak = g->ephemeron = NULL;
  markobject(g, g->mainthread);
  markvalue(g, &g->l_registry);
  markmt(g);
  markbeingfnz(g);  /* mark any finalizing object left from previous cycle */
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/

static void traverseweakvalue (global_State *g, Table *h) {
  /* if there is array part, assume it may have white values (do not
     traverse it just to check) */
  int hasclears = h->hasArray();

  for(int i = 0; i < (int)h->hashtable.size(); i++) {
    Node* n = h->getNode(i);
    checkdeadkey(n);
    if (ttisnil(&n->i_val))  /* entry is empty? */
      removeentry(n);  /* remove it */
    else {
      assert(!ttisnil(&n->i_key));
      markvalue(g, &n->i_key);  /* mark key */
      if (!hasclears && iscleared(&n->i_val))  /* is there a white value? */
        hasclears = 1;  /* table will have to be cleared */
    }
  }
  if (hasclears)
    linktable(h, &g->weak);  /* has to be cleared later */
  else  /* no white values */
    linktable(h, &g->grayagain);  /* no need to clean */
}


static int traverseephemeron (global_State *g, Table *h) {
  int marked = 0;  /* true if an object is marked in this traversal */
  int hasclears = 0;  /* true if table has white keys */
  int prop = 0;  /* true if table has entry "white-key -> white-value" */
  /* traverse array part (numeric keys are 'strong') */
  for (int i = 0; i < (int)h->array.size(); i++) {
    if (valiswhite(&h->array[i])) {
      marked = 1;
      reallymarkobject(g, gcvalue(&h->array[i]));
    }
  }
  /* traverse hash part */
  for (int i = 0; i < (int)h->hashtable.size(); i++) {
    Node* n = h->getNode(i);
    checkdeadkey(n);
    if (ttisnil(&n->i_val))  /* entry is empty? */
      removeentry(n);  /* remove it */
    else if (iscleared(&n->i_key)) {  /* key is not marked (yet)? */
      hasclears = 1;  /* table must be cleared */
      if (valiswhite(&n->i_val))  /* value not marked yet? */
        prop = 1;  /* must propagate again */
    }
    else if (valiswhite(&n->i_val)) {  /* value not marked yet? */
      marked = 1;
      reallymarkobject(g, gcvalue(&n->i_val));  /* mark it now */
    }
  }
  if (prop)
    linktable(h, &g->ephemeron);  /* have to propagate again */
  else if (hasclears)  /* does table have white keys? */
    linktable(h, &g->allweak);  /* may have to clean white keys */
  else  /* no white keys */
    linktable(h, &g->grayagain);  /* no need to clean */
  return marked;
}


static void traversestrongtable (global_State *g, Table *h) {
  for (int i = 0; i < (int)h->array.size(); i++)  /* traverse array part */
    markvalue(g, &h->array[i]);
  for(int i = 0; i < (int)h->hashtable.size(); i++) {
    Node* n = h->getNode(i);
    checkdeadkey(n);
    if (ttisnil(&n->i_val))  /* entry is empty? */
      removeentry(n);  /* remove it */
    else {
      assert(!ttisnil(&n->i_key));
      markvalue(g, &n->i_key);  /* mark key */
      markvalue(g, &n->i_val);  /* mark value */
    }
  }
}


static int traversetable (global_State *g, Table *h) {
  const TValue *mode = fasttm(h->metatable, TM_MODE);
  markobject(g, h->metatable);
  if (mode && ttisstring(mode)) {  /* is there a weak mode? */
    int weakkey = (strchr(tsvalue(mode)->c_str(), 'k') != NULL);
    int weakvalue = (strchr(tsvalue(mode)->c_str(), 'v') != NULL);
    if (weakkey || weakvalue) {  /* is really weak? */
      black2gray(obj2gco(h));  /* keep table gray */
      if (!weakkey) {  /* strong keys? */
        traverseweakvalue(g, h);
        return TRAVCOST + (int)h->hashtable.size();
      }
      else if (!weakvalue) {  /* strong values? */
        traverseephemeron(g, h);
        return TRAVCOST + (int)h->array.size() + (int)h->hashtable.size();
      }
      else {
        linktable(h, &g->allweak);  /* nothing to traverse now */
        return TRAVCOST;
      }
    }  /* else go through */
  }
  traversestrongtable(g, h);
  return TRAVCOST + (int)h->array.size() + (2 * (int)h->hashtable.size());
}


static int traverseproto (global_State *g, Proto *f) {
  if (f->cache && iswhite(obj2gco(f->cache)))
    f->cache = NULL;  /* allow cache to be collected */
  stringmark(f->source);
  for (size_t i = 0; i < f->constants.size(); i++)  /* mark literals */
    markvalue(g, &f->constants[i]);
  for (size_t i = 0; i < f->upvalues.size(); i++)  /* mark upvalue names */
    stringmark(f->upvalues[i].name);
  for (size_t i = 0; i < f->p.size(); i++)  /* mark nested protos */
    markobject(g, f->p[i]);
  for (size_t i = 0; i < f->locvars.size(); i++)  /* mark local-variable names */
    stringmark(f->locvars[i].varname);
  return TRAVCOST +
         (int)f->constants.size() +
         (int)f->upvalues.size() +
         (int)f->p.size() +
         (int)f->locvars.size();
}


static int traverseclosure (global_State *g, Closure *cl) {
  if (cl->isC) {
    int i;
    for (i=0; i<cl->nupvalues; i++)  /* mark its upvalues */
      markvalue(g, &cl->pupvals_[i]);
  }
  else {
    int i;
    assert(cl->nupvalues == cl->p->upvalues.size());
    markobject(g, cl->p);  /* mark its prototype */
    for (i=0; i<cl->nupvalues; i++)  /* mark its upvalues */
      markobject(g, cl->ppupvals_[i]);
  }
  return TRAVCOST + cl->nupvalues;
}


static int traversestack (global_State *g, lua_State *L) {
  StkId o = L->stack.begin();
  if (o == NULL)
    return 1;  /* stack not completely built yet */
  for (; o < L->top; o++)
    markvalue(g, o);
  if (g->gcstate == GCSatomic) {  /* final traversal? */
    StkId lim = L->stack.end();  /* real end of stack */
    for (; o < lim; o++)  /* clear not-marked stack slice */
      setnilvalue(o);
  }
  return TRAVCOST + cast_int(o - L->stack.begin());
}


/*
** traverse one gray object, turning it to black (except for threads,
** which are always gray).
** Returns number of values traversed.
*/
static int propagatemark (global_State *g) {
  LuaObject *o = g->gray;
  assert(isgray(o));
  gray2black(o);
  switch (gch(o)->tt) {
    case LUA_TTABLE: {
      Table *h = gco2t(o);
      g->gray = h->graylist;
      return traversetable(g, h);
    }
    case LUA_TFUNCTION: {
      Closure *cl = gco2cl(o);
      g->gray = cl->gclist;
      return traverseclosure(g, cl);
    }
    case LUA_TTHREAD: {
      lua_State *th = gco2th(o);
      g->gray = th->gclist;
      th->gclist = g->grayagain;
      g->grayagain = o;
      black2gray(o);
      return traversestack(g, th);
    }
    case LUA_TPROTO: {
      Proto *p = gco2p(o);
      g->gray = p->gclist;
      return traverseproto(g, p);
    }
    default: assert(0); return 0;
  }
}


static void propagateall (global_State *g) {
  while (g->gray) propagatemark(g);
}


static void propagatelist (global_State *g, LuaObject *l) {
  assert(g->gray == NULL);  /* no grays left */
  g->gray = l;
  propagateall(g);  /* traverse all elements from 'l' */
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
  g->weak = g->grayagain = g->ephemeron = NULL;
  propagateall(g);  /* traverse main gray list */
  propagatelist(g, grayagain);
  propagatelist(g, weak);
  propagatelist(g, ephemeron);
}


static void convergeephemerons (global_State *g) {
  int changed;
  do {
    LuaObject *w;
    LuaObject *next = g->ephemeron;  /* get ephemeron list */
    g->ephemeron = NULL;  /* tables will return to this list when traversed */
    changed = 0;
    while ((w = next) != NULL) {
      next = gco2t(w)->graylist;
      if (traverseephemeron(g, gco2t(w))) {  /* traverse marked some value? */
        propagateall(g);  /* propagate changes */
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
static void clearkeys (LuaObject *l, LuaObject *f) {
  for (; l != f; l = gco2t(l)->graylist) {
    Table *h = gco2t(l);
    for(int i = 0; i < (int)h->hashtable.size(); i++) {
      Node* n = h->getNode(i);
      if (!ttisnil(&n->i_val) && (iscleared(&n->i_key))) {
        setnilvalue(&n->i_val);  /* remove value ... */
        removeentry(n);  /* and remove entry from table */
      }
    }
  }
}


/*
** clear entries with unmarked values from all weaktables in list 'l' up
** to element 'f'
*/
static void clearvalues (LuaObject *l, LuaObject *f) {
  for (; l != f; l = gco2t(l)->graylist) {
    Table *h = gco2t(l);
    for (int i = 0; i < (int)h->array.size(); i++) {
      TValue *o = &h->array[i];
      if (iscleared(o))  /* value was collected? */
        setnilvalue(o);  /* remove value */
    }
    for(int i = 0; i < (int)h->hashtable.size(); i++) {
      Node* n = h->getNode(i);
      if (!ttisnil(&n->i_val) && iscleared(&n->i_val)) {
        setnilvalue(&n->i_val);  /* remove value ... */
        removeentry(n);  /* and remove entry from table */
      }
    }
  }
}

static void freeobj (LuaObject *o) {
  lua_State *L = thread_L;
  switch (gch(o)->tt) {
    case LUA_TPROTO: delete o; break;
    case LUA_TFUNCTION: delete o; break;
    case LUA_TUPVAL: delete o; break;
    case LUA_TTABLE: delete o; break;
    case LUA_TTHREAD: luaE_freethread(L, gco2th(o)); break;
    case LUA_TUSERDATA: luaS_deludata(gco2u(o)); break;
    case LUA_TSTRING: luaS_freestr(gco2ts(o)); break;
    default: assert(0);
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
  sweepwholelist(&L1->openupval);  /* sweep open upvalues */
  {
    THREAD_CHANGE(L1);
    luaE_freeCI(L1);  /* free extra CallInfo slots */
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
static LuaObject **sweeplist (LuaObject **p, size_t count) {
  global_State *g = thread_G;
  int ow = otherwhite();
  int toclear, toset;  /* bits to clear and to set in all live objects */
  int tostop;  /* stop sweep when this is true */
  l_mem debt = g->GCdebt;  /* current debt */
  if (isgenerational(g)) {  /* generational mode? */
    toclear = ~0;  /* clear nothing */
    toset = bitmask(OLDBIT);  /* set the old bit of all surviving objects */
    tostop = bitmask(OLDBIT);  /* do not sweep old generation */
  }
  else {  /* normal mode */
    toclear = maskcolors;  /* clear all color bits + old bit */
    toset = luaC_white(g);  /* make object white */
    tostop = 0;  /* do not stop */
  }
  while (*p != NULL && count-- > 0) {
    LuaObject *curr = *p;
    int marked = gch(curr)->marked;
    if (isdeadm(ow, marked)) {  /* is 'curr' dead? */
      *p = gch(curr)->next;  /* remove 'curr' from list */
      freeobj(curr);  /* erase 'curr' */
    }
    else {
      if (gch(curr)->tt == LUA_TTHREAD)
        sweepthread(gco2th(curr));  /* sweep thread's upvalues */
      if (testbits(marked, tostop)) {
        static LuaObject *nullp = NULL;
        p = &nullp;  /* stop sweeping this list */
        break;
      }
      /* update marks */
      gch(curr)->marked = cast_byte((marked & toclear) | toset);
      p = &gch(curr)->next;  /* go to next element */
    }
  }
  luaE_setdebt(g, debt);  /* sweeping should not change debt */
  return p;
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
    int hs = g->strt->size / 2;  /* half the size of the string table */
    if (g->strt->nuse < cast(uint32_t, hs))  /* using less than that half? */
      luaS_resize(hs);  /* halve its size */
    g->buff.buffer.clear();
  }
}


static LuaObject *udata2finalize (global_State *g) {
  LuaObject *o = g->tobefnz;  /* get first element */
  assert(isfinalized(o));
  g->tobefnz = gch(o)->next;  /* remove it from 'tobefnz' list */
  gch(o)->next = g->allgc;  /* return it to 'allgc' list */
  g->allgc = o;
  resetbit(gch(o)->marked, SEPARATED);  /* mark that it is not in 'tobefnz' */
  assert(!isold(o));  /* see MOVE OLD rule */
  if (!keepinvariant(g))  /* not keeping invariant? */
    makewhite(g, o);  /* "sweep" object */
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
  const TValue *tm;
  TValue v;
  setgcovalue(&v, udata2finalize(g));
  tm = luaT_gettmbyobj(&v, TM_GC);
  if (tm != NULL && ttisfunction(tm)) {  /* is there a finalizer? */
    int status;
    uint8_t oldah = L->allowhook;
    int running  = g->gcrunning;
    L->allowhook = 0;  /* stop debug hooks during GC metamethod */
    g->gcrunning = 0;  /* avoid GC steps */
    setobj(L->top, tm);  /* push finalizer... */
    setobj(L->top + 1, &v);  /* ... and its argument */
    L->top += 2;  /* and (next line) call the finalizer */
    status = luaD_pcall(L, dothecall, NULL, savestack(L, L->top - 2), 0);
    L->allowhook = oldah;  /* restore hooks */
    g->gcrunning = running;  /* restore state */
    if (status != LUA_OK && propagateerrors) {  /* error while running __gc? */
      if (status == LUA_ERRRUN) {  /* is there an error msg.? */
        luaO_pushfstring(L, "error in __gc metamethod (%s)",
                                        lua_tostring(L, -1));
        status = LUA_ERRGCMM;  /* error in __gc metamethod */
      }
      luaD_throw(status);  /* re-send error */
    }
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
    lastnext = &gch(*lastnext)->next;
  while ((curr = *p) != NULL) {  /* traverse all finalizable objects */
    assert(!isfinalized(curr));
    assert(testbit(gch(curr)->marked, SEPARATED));
    if (!(all || iswhite(curr)))  /* not being collected? */
      p = &gch(curr)->next;  /* don't bother with it */
    else {
      l_setbit(gch(curr)->marked, FINALIZEDBIT); /* won't be finalized again */
      *p = gch(curr)->next;  /* remove 'curr' from 'finobj' list */
      gch(curr)->next = *lastnext;  /* link at the end of 'tobefnz' list */
      *lastnext = curr;
      lastnext = &gch(curr)->next;
    }
  }
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/
void luaC_checkfinalizer (LuaObject *o, Table *mt) {
  global_State *g = thread_G;
  if (testbit(gch(o)->marked, SEPARATED) || /* obj. is already separated... */
      isfinalized(o) ||                           /* ... or is finalized... */
      fasttm(mt, TM_GC) == NULL)                /* or has no finalizer? */
    return;  /* nothing to be done */
  else {  /* move 'o' to 'finobj' list */
    LuaObject **p;
    for (p = &g->allgc; *p != o; p = &gch(*p)->next) ;
    *p = gch(o)->next;  /* remove 'o' from root list */
    gch(o)->next = g->finobj;  /* link it in list 'finobj' */
    g->finobj = o;
    l_setbit(gch(o)->marked, SEPARATED);  /* mark it as such */
    resetoldbit(o);  /* see MOVE OLD rule */
  }
}

/* }====================================================== */


/*
** {======================================================
** GC control
** =======================================================
*/


#define sweepphases  \
	(bitmask(GCSsweepstring) | bitmask(GCSsweepudata) | bitmask(GCSsweep))

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
    g->lastmajormem = gettotalbytes(g);
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
    resetoldbit(g->tobefnz);
    GCTM(propagateerrors);
  }
}


void luaC_freeallobjects () {
  global_State *g = thread_G;
  int i;
  separatetobefnz(1);  /* separate all objects with finalizers */
  assert(g->finobj == NULL);
  callallpendingfinalizers(0);
  g->currentwhite = WHITEBITS; /* this "white" makes all objects look dead */
  g->gckind = KGC_NORMAL;
  sweepwholelist(&g->finobj);  /* finalizers can create objs. in 'finobj' */
  sweepwholelist(&g->allgc);
  for (i = 0; i < g->strt->size; i++)  /* free all string lists */
    sweepwholelist(&g->strt->hash[i]);
  assert(g->strt->nuse == 0);
}


static void atomic () {
  global_State *g = thread_G;
  LuaObject *origweak, *origall;
  assert(!iswhite(obj2gco(g->mainthread)));
  markobject(g, thread_L);  /* mark running thread */
  /* registry and global metatables may be changed by API */
  markvalue(g, &g->l_registry);
  markmt(g);  /* mark basic metatables */
  /* remark occasional upvalues of (maybe) dead threads */
  remarkupvals(g);
  /* traverse objects caught by write barrier and by 'remarkupvals' */
  retraversegrays(g);
  convergeephemerons(g);
  /* at this point, all strongly accessible objects are marked. */
  /* clear values from weak tables, before checking finalizers */
  clearvalues(g->weak, NULL);
  clearvalues(g->allweak, NULL);
  origweak = g->weak; origall = g->allweak;
  separatetobefnz(0);  /* separate objects to be finalized */
  markbeingfnz(g);  /* mark userdata that will be finalized */
  propagateall(g);  /* remark, to propagate `preserveness' */
  convergeephemerons(g);
  /* at this point, all resurrected objects are marked. */
  /* remove dead objects from weak tables */
  clearkeys(g->ephemeron, NULL);  /* clear keys from all ephemeron tables */
  clearkeys(g->allweak, NULL);  /* clear keys from all allweak tables */
  /* clear values from resurrected weak tables */
  clearvalues(g->weak, origweak);
  clearvalues(g->allweak, origall);
  g->sweepstrgc = 0;  /* prepare to sweep strings */
  g->gcstate = GCSsweepstring;
  g->currentwhite = cast_byte(otherwhite());  /* flip current white */
}


static l_mem singlestep () {
  global_State *g = thread_G;
  switch (g->gcstate) {
    case GCSpause: {
      if (!isgenerational(g))
        markroot(g);  /* start a new collection */
      /* in any case, root must be marked */
      assert(!iswhite(obj2gco(g->mainthread))
              && !iswhite(gcvalue(&g->l_registry)));
      g->gcstate = GCSpropagate;
      return GCROOTCOST;
    }
    case GCSpropagate: {
      if (g->gray)
        return propagatemark(g);
      else {  /* no more `gray' objects */
        g->gcstate = GCSatomic;  /* finish mark phase */
        atomic();
        return GCATOMICCOST;
      }
    }
    case GCSsweepstring: {
      if (g->sweepstrgc < g->strt->size) {
        sweepwholelist(&g->strt->hash[g->sweepstrgc++]);
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
        LuaObject *mt = obj2gco(g->mainthread);
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
  while (!testbit(statesmask, g->gcstate))
    singlestep();
}


static void generationalcollection () {
  global_State *g = thread_G;
  if (g->lastmajormem == 0) {  /* signal for another major collection? */
    luaC_fullgc(0);  /* perform a full regular collection */
    g->lastmajormem = gettotalbytes(g);  /* update control */
  }
  else {
    luaC_runtilstate(~bitmask(GCSpause));  /* run complete cycle */
    luaC_runtilstate(bitmask(GCSpause));
    if (gettotalbytes(g) > g->lastmajormem/100 * g->gcmajorinc)
      g->lastmajormem = 0;  /* signal for a major collection */
  }
  luaE_setdebt(g, stddebt(g));
}


static void step () {
  global_State *g = thread_G;
  l_mem lim = g->gcstepmul;  /* how much to work */
  do {  /* always perform at least one single step */
    lim -= singlestep();
  } while (lim > 0 && g->gcstate != GCSpause);
  if (g->gcstate != GCSpause)
    luaE_setdebt(g, g->GCdebt - GCSTEPSIZE);
  else
    luaE_setdebt(g, stddebt(g));
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
  if(thread_G->GCdebt > 0) {
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
  luaE_setdebt(g, stddebt(g));
  if (!isemergency)   /* do not run finalizers during emergency GC */
    callallpendingfinalizers(1);
}

/* }====================================================== */


