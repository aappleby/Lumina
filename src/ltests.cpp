/*
** $Id: ltests.c,v 2.124 2011/11/09 19:08:07 roberto Exp $
** Internal Module for Debugging of the Lua Implementation
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaConversions.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "lauxlib.h"
#include "lcode.h"
#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lualib.h"


#define obj_at(L,k)	(L->stack_.callinfo_->getFunc() + (k))


static void setnameval (LuaThread *L, const char *name, int val) {
  THREAD_CHECK(L);
  lua_pushstring(L, name);
  lua_pushinteger(L, val);
  lua_settable(L, -3);
}


static void pushobject (LuaThread *L, const LuaValue *o) {
  THREAD_CHECK(L);
  L->stack_.push(*o);
}


static int tpanic (LuaThread *L) {
  THREAD_CHECK(L);
  fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
                   lua_tostring(L, -1));
  // do not return to Lua
  exit(EXIT_FAILURE);
}


/*
** {======================================================
** Functions to check memory consistency
** =======================================================
*/


static int testobjref1 (LuaVM *g, LuaObject *f, LuaObject *t) {
  if (t->isDead()) return 0;

  if (isgenerational(g)) {
    return !(f->isBlack() && t->isLiveColor());
  }

  if (issweepphase(g)) return 1;

  return !(f->isBlack() && t->isLiveColor());
}


static void printobj (LuaVM *g, LuaObject *o) {
  int i = -1;
  int index = 0;
  for(LuaList::iterator it = g->allgc.begin(); it; ++it, ++index) {
    if(it == o) {
      i = index;
      break;
    }
  }

  char c = 'g';
  if(o->isDead()) c = 'd';
  if(o->isBlack()) c = 'b';
  if(o->isWhite()) c = 'w';

  printf("%d:%s(%p)-%c", i, o->typeName(), (void *)o, c);
}


static int testobjref (LuaVM *g, LuaObject *f, LuaObject *t) {
  int r = testobjref1(g,f,t);
  if (!r) {
    printf("%d(%02X) - ", g->gcstate, g->livecolor);
    printobj(g, f);
    printf("\t-> ");
    printobj(g, t);
    printf("\n");
  }
  return r;
}

#define checkobjref(g,f,t) assert(testobjref(g,f,t))


static void checkvalref (LuaVM *g, LuaObject *f, const LuaValue *t) {
  if (t->isCollectable()) {
    t->typeCheck();
    assert(testobjref(g, f, t->getObject()));
  }
}

void checkTableCallback ( const LuaValue& key, const LuaValue& val, void* blob ) {
  LuaTable* t = (LuaTable*)blob;
  if (!val.isNil()) {
    assert(!key.isNil());
    checkvalref(thread_G, t, &key);
    checkvalref(thread_G, t, &val);
  }
}

static void checktable (LuaVM *g, LuaTable *h) {
  if (h->metatable)
    checkobjref(g, h, h->metatable);

  h->traverse(checkTableCallback,h);
}


/*
** All marks are conditional because a GC may happen while the
** prototype is still being created
*/
static void checkproto (LuaVM *g, LuaProto *f) {
  int i;
  if (f->source) checkobjref(g, f, f->source);
  for (i=0; i < (int)f->constants.size(); i++) {
    if (f->constants[i].isString())
      checkobjref(g, f, f->constants[i].getString());
  }
  for (i=0; i < (int)f->upvalues.size(); i++) {
    if (f->upvalues[i].name)
      checkobjref(g, f, f->upvalues[i].name);
  }
  for (i=0; i < (int)f->subprotos_.size(); i++) {
    if (f->subprotos_[i]) {
      checkobjref(g, f, f->subprotos_[i]);
    }
  }
  for (i=0; i < (int)f->locvars.size(); i++) {
    if (f->locvars[i].varname)
      checkobjref(g, f, f->locvars[i].varname);
  }
}


static void checkCClosure (LuaVM *g, LuaClosure *cl) {
  int i;
  for (i=0; i<cl->nupvalues; i++) {
    checkvalref(g, cl, &cl->pupvals_[i]);
  }
}


static void checkLClosure (LuaVM *g, LuaClosure *cl) {
  int i;
  assert(cl->nupvalues == (int)cl->proto_->upvalues.size());
  checkobjref(g, cl, cl->proto_);
  for (i=0; i<cl->nupvalues; i++) {
    if (cl->ppupvals_[i]) {
      assert(cl->ppupvals_[i]->isUpval());
      checkobjref(g, cl, cl->ppupvals_[i]);
    }
  }
}

static void checkobject (LuaVM *g, LuaObject *o) {
  if (o->isDead()) {
    assert(issweepphase(g));
    return;
  }

  if (g->gcstate == GCSpause && !isgenerational(g)) {
    assert(o->isWhite());
  }

  if(o->isUpval()) {
    LuaUpvalue *uv = dynamic_cast<LuaUpvalue*>(o);
    assert(uv->v == &uv->value);  /* must be closed */
    assert(!o->isGray());  /* closed upvalues are never gray */
    checkvalref(g, o, uv->v);
    return;
  }

  if(o->isUserdata()) {
    LuaTable *mt = dynamic_cast<LuaBlob*>(o)->metatable_;
    if (mt) checkobjref(g, o, mt);
    return;
  }

  if(o->isTable()) {
    checktable(g, dynamic_cast<LuaTable*>(o));
    return;
  }

  if(o->isThread()) {
    LuaThread* l = dynamic_cast<LuaThread*>(o);
    assert(l);
    return;
  }

  if(o->isLClosure() || o->isCClosure()) {
    LuaClosure* c = dynamic_cast<LuaClosure*>(o);
    if(c->isC) {
      checkCClosure(g, c);
    } else {
      checkLClosure(g, c);
    }
    return;
  }

  if(o->isProto()) {
    checkproto(g, dynamic_cast<LuaProto*>(o));
    return;
  }

  if(o->isString()) {
    return;
  }

  assert(0);
}



/*
** mark all objects in gray lists as TestGray, so that
** 'checkmemory' can check that all gray objects are in a gray list
*/
void CheckGraylistCB(LuaObject* o) {
  assert(o->isGray());
  assert(!o->isTestGray());
  o->setTestGray();
  o = o->getNextGray();
}

static void markTestGrays (LuaVM *g) {
  g->gc_.grayhead_.Traverse(CheckGraylistCB);
  g->gc_.grayagain_.Traverse(CheckGraylistCB);
  g->gc_.weak_.Traverse(CheckGraylistCB);
  g->gc_.ephemeron_.Traverse(CheckGraylistCB);
  g->gc_.allweak_.Traverse(CheckGraylistCB);
}


static void checkold (LuaVM *g, LuaObject *o) {
  int isold = 0;
  for (; o != NULL; o = o->getNext()) {
    if (o->isOld()) {  /* old generation? */
      assert(isgenerational(g));
      if (!issweepphase(g))
        isold = 1;
    }
    else assert(!isold);  /* non-old object cannot be after an old one */
    if (o->isGray()) {
      assert(!keepinvariant(g) || o->isTestGray());
      o->clearTestGray();
    }
    assert(!o->isTestGray());
  }
}

static void checkold (LuaVM *g, LuaList& list) {
  int isold = 0;
  for(LuaList::iterator it = list.begin(); it; ++it) {
    if (it->isOld()) {  /* old generation? */
      assert(isgenerational(g));
      if (!issweepphase(g))
        isold = 1;
    }
    else assert(!isold);  /* non-old object cannot be after an old one */
    if (it->isGray()) {
      assert(!keepinvariant(g) || it->isTestGray());
      it->clearTestGray();
    }
    assert(!it->isTestGray());
  }
}


int lua_checkmemory (LuaThread *L) {
  THREAD_CHECK(L);
  LuaVM *g = G(L);
  LuaUpvalue *uv;
  if (keepinvariant(g)) {
    assert(!g->mainthread->isWhite());
    assert(!g->l_registry.getObject()->isWhite());
  }
  assert(!g->l_registry.getObject()->isDead());
  g->mainthread->clearTestGray();
  
  /* check 'allgc' list */
  if (keepinvariant(g)) {
    markTestGrays(g);
  }

  checkold(g, g->allgc);
  for(LuaList::iterator it = g->allgc.begin(); it; ++it) {
    checkobject(g, it);
    assert(!it->isSeparated());
  }
  /* check 'finobj' list */
  checkold(g, g->finobj);

  for(LuaList::iterator it = g->finobj.begin(); it; ++it) {
    assert(!it->isDead() && it->isSeparated());
    assert(it->isUserdata() || it->isTable());
    checkobject(g, it);
  }

  /* check 'tobefnz' list */
  checkold(g, g->tobefnz.begin());
  for (LuaList::iterator it = g->tobefnz.begin(); it; ++it) {
    assert(!it->isWhite());
    assert(!it->isDead() && it->isSeparated());
    assert(it->isUserdata() || it->isTable());

    if(it->getNext()) assert(it->getNext()->getPrev() == it);
    if(it->getPrev()) assert(it->getPrev()->getNext() == it);
  }
  /* check 'uvhead' list */
  for (uv = g->uvhead.unext; uv != &g->uvhead; uv = uv->unext) {
    assert(uv->unext->uprev == uv && uv->uprev->unext == uv);
    assert(uv->v != &uv->value);  /* must be open */
    assert(!uv->isBlack());  /* open upvalues are never black */
    if (uv->isDead())
      assert(issweepphase(g));
    else
      checkvalref(g, uv, uv->v);
  }
  return 0;
}

/* }====================================================== */



/*
** {======================================================
** Disassembler
** =======================================================
*/


static char *buildop (LuaProto *p, int pc, char *buff) {
  Instruction i = p->instructions_[pc];
  OpCode o = GET_OPCODE(i);
  const char *name = luaP_opnames[o];
  int line = p->getLine(pc);
  sprintf(buff, "(%4d) %4d - ", line, pc);
  switch (getOpMode(o)) {
    case iABC:
      sprintf(buff+strlen(buff), "%-12s%4d %4d %4d", name,
              GETARG_A(i), GETARG_B(i), GETARG_C(i));
      break;
    case iABx:
      sprintf(buff+strlen(buff), "%-12s%4d %4d", name, GETARG_A(i), GETARG_Bx(i));
      break;
    case iAsBx:
      sprintf(buff+strlen(buff), "%-12s%4d %4d", name, GETARG_A(i), GETARG_sBx(i));
      break;
    case iAx:
      sprintf(buff+strlen(buff), "%-12s%4d", name, GETARG_Ax(i));
      break;
  }
  return buff;
}


static int listcode (LuaThread *L) {
  THREAD_CHECK(L);
  int pc;
  LuaProto *p;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = obj_at(L, 1)->getLClosure()->proto_;

  lua_createtable(L, 0, 0);

  setnameval(L, "maxstack", p->maxstacksize);
  setnameval(L, "numparams", p->numparams);
  for (pc=0; pc < (int)p->instructions_.size(); pc++) {
    char buff[100];
    lua_pushinteger(L, pc+1);
    lua_pushstring(L, buildop(p, pc, buff));
    lua_settable(L, -3);
  }
  return 1;
}


static int listk (LuaThread *L) {
  THREAD_CHECK(L);
  LuaProto *p;
  int i;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = obj_at(L, 1)->getLClosure()->proto_;
  lua_createtable(L, (int)p->constants.size(), 0);
  for (i=0; i < (int)p->constants.size(); i++) {
    pushobject(L, &p->constants[i]);
    lua_rawseti(L, -2, i+1);
  }
  return 1;
}


static int listlocals (LuaThread *L) {
  THREAD_CHECK(L);
  LuaProto *p;
  int pc = luaL_checkint(L, 2) - 1;
  int i = 0;
  const char *name;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = obj_at(L, 1)->getLClosure()->proto_;
  while ((name = p->getLocalName(++i, pc)) != NULL) {
    lua_pushstring(L, name);
  }
  return i-1;
}

/* }====================================================== */




static int get_limits (LuaThread *L) {
  THREAD_CHECK(L);
  lua_createtable(L, 0, 5);
  setnameval(L, "BITS_INT", 32);
  setnameval(L, "LFPF", LFIELDS_PER_FLUSH);
  setnameval(L, "MAXSTACK", MAXSTACK);
  setnameval(L, "NUM_OPCODES", NUM_OPCODES);
  return 1;
}


static int mem_query (LuaThread *L) {
  THREAD_CHECK(L);
  if (lua_isnone(L, 1)) {
    lua_pushinteger(L, l_memcontrol.mem_total);
    lua_pushinteger(L, l_memcontrol.mem_blocks);
    lua_pushinteger(L, l_memcontrol.mem_max);
    return 3;
  }
  else if (lua_isNumberable(L, 1)) {
    l_memcontrol.mem_limit = luaL_checkint(L, 1);
    return 0;
  }
  else {
    const char *t = luaL_checkstring(L, 1);
    int i;
    int total = 0;
    bool found = false;
    for (i = LUA_NUMTAGS - 1; i >= 0; i--) {
      if (strcmp(t, ttypename(i)) == 0) {
        total += thread_G->instanceCounts[i];
        found = true;
      }
    }
    if(found) {
      lua_pushinteger(L, total);
      return 1;
    }
    return luaL_error(L, "unkown type '%s'", t);
  }
}


static int get_gccolor (LuaThread *L) {
  THREAD_CHECK(L);
  LuaValue *o;
  luaL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!o->isCollectable()) {
    lua_pushstring(L, "no collectable");
  }
  else {
    LuaObject* lo = o->getObject();
    int n = 1;
    lua_pushstring(L, lo->isWhite() ? "white" :
                      lo->isBlack() ? "black" : "grey");
    if (lo->isFinalized()) {
      lua_pushliteral(L, "/finalized"); n++;
    }
    if (lo->isSeparated()) {
      lua_pushliteral(L, "/separated"); n++;
    }
    if (lo->isFixed()) {
      lua_pushliteral(L, "/fixed"); n++;
    }
    if (lo->isOld()) {
      lua_pushliteral(L, "/old"); n++;
    }
    lua_concat(L, n);
  }
  return 1;
}


static int gc_state (LuaThread *L) {
  THREAD_CHECK(L);
  static const char *statenames[] = {"propagate", "atomic",
    "sweepstring", "sweepudata", "sweep", "pause", ""};
  int option = luaL_checkoption(L, 1, "", statenames);
  if (option == GCSpause + 1) {
    lua_pushstring(L, statenames[thread_G->gcstate]);
    return 1;
  }
  else {
    luaC_runtilstate(1 << option);
    assert(thread_G->gcstate == option);
    return 0;
  }
}

static int stacklevel (LuaThread *L) {
  THREAD_CHECK(L);
  unsigned long a = 0;
  lua_pushinteger(L, (L->stack_.top_ - L->stack_.begin()));
  lua_pushinteger(L, (L->stack_.last() - L->stack_.begin()));
  lua_pushinteger(L, (unsigned long)&a);
  return 5;
}


static int table_query (LuaThread *L) {
  THREAD_CHECK(L);
  LuaTable *t;
  int i = luaL_optint(L, 2, -1);
  luaL_checktype(L, 1, LUA_TTABLE);
  t = obj_at(L, 1)->getTable();
  if (i == -1) {
    lua_pushinteger(L, (int)t->getArraySize());
    lua_pushinteger(L, (int)t->getHashSize());
    return 2;
  }
  else if (i < (int)t->getArraySize()) {
    LuaValue val;
    t->getArrayElement(i, val);
    lua_pushinteger(L, i);
    pushobject(L, &val);
    return 2;
  }
  else if ((i -= (int)t->getArraySize()) < (int)t->getHashSize()) {
    LuaValue key,val;
    t->getHashElement(i,key,val);
    if (val.isNotNil() || key.isNil() || key.isNumber()) {
      pushobject(L, &key);
    }
    else {
      lua_pushliteral(L, "<undef>");
    }
    pushobject(L, &val);
    return 2;
  }
  assert(false);
  return 0;
}


static int string_query (LuaThread *L) {
  THREAD_CHECK(L);
  LuaStringTable *tb = G(L)->strings_;
  int s = luaL_optint(L, 2, 0) - 1;
  if (s==-1) {
    lua_pushinteger(L ,tb->getStringCount());
    lua_pushinteger(L ,tb->getHashSize());
    return 2;
  }
  else if (s < tb->getHashSize()) {
    LuaObject *ts;
    int n = 0;
    for (ts = tb->getStringAt(s); ts; ts = ts->getNext()) {
      LuaResult result = L->stack_.push_reserve2(LuaValue(ts));
      handleResult(result);
      n++;
    }
    return n;
  }
  return 0;
}


static int tref (LuaThread *L) {
  THREAD_CHECK(L);
  int level = L->stack_.getTopIndex();
  luaL_checkany(L, 1);
  L->stack_.copy(1);
  lua_pushinteger(L, luaL_ref(L));
  assert(L->stack_.getTopIndex() == level+1);  /* +1 for result */
  return 1;
}

static int getref (LuaThread *L) {
  THREAD_CHECK(L);
  int level = L->stack_.getTopIndex();
  
  LuaValue key = L->stack_.top_[-1];
  int ref = key.isInteger() ? key.getInteger() : 0;
  LuaTable* registry = L->l_G->getRegistry();
  LuaValue val = registry->get( LuaValue(ref) );
  if(val.isNone()) val = LuaValue::Nil();
  L->stack_.push(val);

  assert(L->stack_.getTopIndex() == level+1);
  return 1;
}

static int unref (LuaThread *L) {
  THREAD_CHECK(L);
  int level = L->stack_.getTopIndex();
  luaL_unref(L, luaL_checkint(L, 1));
  assert(L->stack_.getTopIndex() == level);
  return 0;
}


static int upvalue (LuaThread *L) {
  THREAD_CHECK(L);
  int n = luaL_checkint(L, 2);
  luaL_checkIsFunction(L, 1);
  if (lua_isnone(L, 3)) {
    const char *name = lua_getupvalue(L, 1, n);
    if (name == NULL) return 0;
    lua_pushstring(L, name);
    return 2;
  }
  else {
    const char *name = lua_setupvalue(L, 1, n);
    lua_pushstring(L, name);
    return 1;
  }
}

static int newuserdata (LuaThread *L) {
  THREAD_CHECK(L);
  size_t size = luaL_checkint(L, 1);
  // TODO(aappleby): This limit of 1 gigabyte for a userdata is arbitrary.
  if(size > 1000000000) throwError(LUA_ERRMEM);
  char *p = cast(char *, lua_newuserdata(L, size));
  while (size--) *p++ = '\0';
  return 1;
}


static int pushuserdata (LuaThread *L) {
  THREAD_CHECK(L);
  void* p = (void*)luaL_checkinteger(L, 1);
  L->stack_.push( LuaValue::Pointer(p) );
  return 1;
}


static int udataval (LuaThread *L) {
  THREAD_CHECK(L);
  LuaValue v = L->stack_.at(1);

  if(v.isBlob()) {
    L->stack_.push( (int)v.getBlob()->buf_ );
    return 1;
  }
  if(v.isPointer()) {
    L->stack_.push( (int)v.getPointer() );
    return 1;
  }
  return 0;
}


static int doonnewstack (LuaThread *L) {
  THREAD_CHECK(L);
  LuaThread *L1 = lua_newthread(L);
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  int status;
  {
    THREAD_CHANGE(L1);
    status = luaL_loadbuffer(L1, s, l, s);
    if (status == LUA_OK)
      status = lua_pcall(L1, 0, 0, 0);
  }
  lua_pushinteger(L, status);
  return 1;
}


static int s2d (LuaThread *L) {
  THREAD_CHECK(L);
  lua_pushnumber(L, *cast(const double *, luaL_checkstring(L, 1)));
  return 1;
}


static int d2s (LuaThread *L) {
  THREAD_CHECK(L);
  double d = luaL_checknumber(L, 1);
  lua_pushlstring(L, cast(char *, &d), sizeof(d));
  return 1;
}


static int num2int (LuaThread *L) {
  THREAD_CHECK(L);
  lua_pushinteger(L, lua_tointeger(L, 1));
  return 1;
}


static int newstate (LuaThread *L) {
  THREAD_CHECK(L);
  LuaThread *L1 = lua_newstate();
  if (L1) {
    GLOBAL_CHANGE(L1);
    lua_atpanic(L1, tpanic);
    {
      GLOBAL_CHANGE(L);
      L->stack_.push( LuaValue::Pointer(L1) );
    }
  }
  else {
    L->stack_.push(LuaValue::Nil());
  }
  return 1;
}


static int loadlib (LuaThread *L) {
  THREAD_CHECK(L);
  static const luaL_Reg libs[] = {
    {"_G", luaopen_base},
    {"coroutine", luaopen_coroutine},
    {"debug", luaopen_debug},
    {"io", luaopen_io},
    {"math", luaopen_math},
    {"string", luaopen_string},
    {"table", luaopen_table},
    {NULL, NULL}
  };
  LuaThread *L1 = (LuaThread *)L->stack_.at(1).getPointer();
  int i;
  {
    GLOBAL_CHANGE(L1);
    luaopen_package(L1);
    luaL_getregistrytable(L1, "_PRELOAD");
    for (i = 0; libs[i].name; i++) {
      L1->stack_.push( libs[i].func );
      lua_setfield(L1, -2, libs[i].name);
    }
  }
  return 0;
}

static int closestate (LuaThread *L) {
  THREAD_CHECK(L);
  LuaThread *L1 = (LuaThread *)L->stack_.at(1).getPointer();
  {
    GLOBAL_CHANGE(L1);
    lua_close(L1);
  }
  return 0;
}

static int doremote (LuaThread *L) {
  THREAD_CHECK(L);
  LuaThread *L1 = (LuaThread *)L->stack_.at(1).getPointer();
  size_t lcode;
  const char *code = luaL_checklstring(L, 2, &lcode);
  int status;
  {
    GLOBAL_CHANGE(L1);
    L1->stack_.setTopIndex(0);
    status = luaL_loadbuffer(L1, code, lcode, code);
    if (status == LUA_OK)
      status = lua_pcall(L1, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
      const char * result = lua_tostring(L1, -1);
      {
        GLOBAL_CHANGE(L);
        L->stack_.push(LuaValue::Nil());
        lua_pushstring(L, result);
        lua_pushinteger(L, status);
      }
      return 3;
    }
    else {
      int i = 0;
      while (!lua_isnone(L1, ++i)) {
        const char* result = lua_tostring(L1, i);
        {
          GLOBAL_CHANGE(L);
          lua_pushstring(L, result);
        }
      }
      L1->stack_.pop(i-1);
      return i-1;
    }
  }
}


static int int2fb_aux (LuaThread *L) {
  THREAD_CHECK(L);
  int b = luaO_int2fb(luaL_checkint(L, 1));
  lua_pushinteger(L, b);
  lua_pushinteger(L, luaO_fb2int(b));
  return 2;
}



/*
** {======================================================
** function to test the API with C. It interprets a kind of assembler
** language with calls to the API, so the test can be driven by Lua code
** =======================================================
*/


static void sethookaux (LuaThread *L, int mask, int count, const char *code);

static const char *const delimits = " \t\n,;";

static void skip (const char **pc) {
  for (;;) {
    if (**pc != '\0' && strchr(delimits, **pc)) {
      (*pc)++;
    }
    else if (**pc == '#') {
      while (**pc != '\n' && **pc != '\0') (*pc)++;
    }
    else break;
  }
}

static int getnum_aux (LuaThread *L, LuaThread *L1, const char **pc) {
  THREAD_CHECK(L);
  int res = 0;
  int sig = 1;
  skip(pc);
  if (**pc == '.') {
    {
      GLOBAL_CHANGE(L1);
      res = (int)lua_tointeger(L1, -1);
      L1->stack_.pop();
    }
    (*pc)++;
    return res;
  }
  else if (**pc == '-') {
    sig = -1;
    (*pc)++;
  }
  if (!lisdigit(cast_uchar(**pc))) {
    luaL_error(L, "number expected (%s)", *pc);
  }
  while (lisdigit(cast_uchar(**pc))) res = res*10 + (*(*pc)++) - '0';
  return sig*res;
}

static const char *getstring_aux (LuaThread *L, char *buff, const char **pc) {
  THREAD_CHECK(L);
  int i = 0;
  skip(pc);
  if (**pc == '"' || **pc == '\'') {  /* quoted string? */
    int quote = *(*pc)++;
    while (**pc != quote) {
      if (**pc == '\0') luaL_error(L, "unfinished string in C script");
      buff[i++] = *(*pc)++;
    }
    (*pc)++;
  }
  else {
    while (**pc != '\0' && !strchr(delimits, **pc))
      buff[i++] = *(*pc)++;
  }
  buff[i] = '\0';
  return buff;
}


static int getindex_aux (LuaThread *L, LuaThread *L1, const char **pc) {
  THREAD_CHECK(L);
  skip(pc);
  switch (*(*pc)++) {
    case 'R': return LUA_REGISTRYINDEX;
    case 'G': return luaL_error(L, "deprecated index 'G'");
    case 'U': return lua_upvalueindex(getnum_aux(L, L1, pc));
    default: (*pc)--; return getnum_aux(L, L1, pc);
  }
}


static void pushcode (LuaThread *L, int code) {
  THREAD_CHECK(L);
  static const char *const codes[] = {"OK", "YIELD", "ERRRUN",
                   "ERRSYNTAX", "ERRMEM", "ERRGCMM", "ERRERR"};
  lua_pushstring(L, codes[code]);
}


#define EQ(s1)	(strcmp(s1, inst) == 0)

#define getnum		(getnum_aux(L, L1, &pc))
#define getstring	(getstring_aux(L, buff, &pc))
#define getindex	(getindex_aux(L, L1, &pc))


static int testC (LuaThread *L);
static int Cfunck (LuaThread *L);

static int runC (LuaThread *L, LuaThread *L1, const char *pc) {
  THREAD_CHECK(L);
  char buff[300];
  int status = 0;
  int tempindex;
  const char* tempstring;
  int tempnum;
  if (pc == NULL) return luaL_error(L, "attempt to runC null script");
  for (;;) {
    GLOBAL_CHANGE(L1);
    const char *inst;
    {
      GLOBAL_CHANGE(L);
      inst = getstring;
    }
    if EQ("") return 0;
    else if EQ("absindex") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushnumber(L1, lua_absindex(L1, tempindex));
    }
    else if EQ("isnumber") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_isNumberable(L1, tempindex));
    }
    else if EQ("isstring") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_isStringable(L1, tempindex));
    }
    else if EQ("istable") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_istable(L1, tempindex));
    }
    else if EQ("iscfunction") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_iscfunction(L1, tempindex));
    }
    else if EQ("isfunction") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_isfunction(L1, tempindex));
    }
    else if EQ("isuserdata") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      LuaValue v = L1->stack_.at(tempindex);
      bool result = v.isBlob() || v.isPointer();
      lua_pushboolean(L1, result);
    }
    else if EQ("isudataval") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_islightuserdata(L1, tempindex));
    }
    else if EQ("isnil") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_isnil(L1, tempindex));
    }
    else if EQ("isnull") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_isnone(L1, tempindex));
    }
    else if EQ("tonumber") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushnumber(L1, lua_tonumber(L1, tempindex));
    }
    else if EQ("topointer") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      const void* temp1 = lua_topointer(L1, tempindex);
      size_t temp2 = reinterpret_cast<size_t>(temp1);
      lua_pushnumber(L1, static_cast<double>(temp2));
    }
    else if EQ("tostring") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      const char *s = lua_tostring(L1, tempindex);
      const char *s1 = lua_pushstring(L1, s);
      assert((s == NULL && s1 == NULL) || (strcmp)(s, s1) == 0);
    }
    else if EQ("objsize") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushinteger(L1, lua_rawlen(L1, tempindex));
    }
    else if EQ("len") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_len(L1, tempindex);
    }
    else if EQ("Llen") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushinteger(L1, luaL_len(L1, tempindex));
    }
    else if EQ("tocfunction") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      LuaCallback c = lua_tocfunction(L1, tempindex);
      L1->stack_.push(c);
    }
    else if EQ("func2num") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      LuaCallback func = lua_tocfunction(L1, tempindex);
      size_t temp = reinterpret_cast<size_t>(func);
      lua_pushnumber(L1, static_cast<double>(temp));
    }
    else if EQ("return") {
      int n;
      { GLOBAL_CHANGE(L); n = getnum; }
      if (L1 != L) {
        int i;
        for (i = 0; i < n; i++) {
          const char* result = lua_tostring(L1, -(n - i));
          {
            GLOBAL_CHANGE(L);
            lua_pushstring(L, result);
          }
        }
      }
      return n;
    }
    else if EQ("gettop") {
      lua_pushinteger(L1, L1->stack_.getTopIndex());
    }
    else if EQ("settop") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      L1->stack_.setTopIndex(tempnum);
    }
    else if EQ("pop") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      L1->stack_.pop(tempnum);
    }
    else if EQ("pushnum") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_pushinteger(L1, tempnum);
    }
    else if EQ("pushstring") {
      { GLOBAL_CHANGE(L); tempstring = getstring; }
      lua_pushstring(L1, tempstring);
    }
    else if EQ("pushnil") {
      L1->stack_.push(LuaValue::Nil());
    }
    else if EQ("pushbool") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_pushboolean(L1, tempnum);
    }
    else if EQ("newtable") {
      lua_createtable(L1, 0, 0);
    }
    else if EQ("newuserdata") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_newuserdata(L1, tempnum);
    }
    else if EQ("tobool") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_pushboolean(L1, lua_toboolean(L1, tempindex));
    }
    else if EQ("pushvalue") {
      {
        GLOBAL_CHANGE(L);
        tempindex = getindex;
      }
      if(tempindex == LUA_REGISTRYINDEX) {
        L1->stack_.push( LuaValue(L1->l_G->getRegistry()) );
      }
      else {
        L1->stack_.copy(tempindex);
      }
    }
    else if EQ("pushcclosure") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_pushcclosure(L1, testC, tempnum);
    }
    else if EQ("pushupvalueindex") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_pushinteger(L1, lua_upvalueindex(tempnum));
    }
    else if EQ("remove") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      L1->stack_.remove(tempnum);
    }
    else if EQ("insert") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_insert(L1, tempnum);
    }
    else if EQ("replace") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_replace(L1, tempindex);
    }
    else if EQ("copy") {
      int f;
      { GLOBAL_CHANGE(L); f = getindex; tempindex = getindex; }
      lua_copy(L1, f, tempindex);
    }
    else if EQ("gettable") {
      {
        GLOBAL_CHANGE(L);
        tempindex = getindex;
      }
      if(tempindex == LUA_REGISTRYINDEX) {
        LuaValue key = L1->stack_.top_[-1];
        L1->stack_.pop();
        LuaValue val = L1->l_G->getRegistry()->get(key);
        L1->stack_.push(val);
      }
      else {
        lua_gettable(L1, tempindex);
      }
    }
    else if EQ("getglobal") {
      { GLOBAL_CHANGE(L); tempstring = getstring; }
      lua_getglobal(L1, tempstring);
    }
    else if EQ("getfield") {
      int t;
      { GLOBAL_CHANGE(L); t = getindex; tempstring = getstring; }
      lua_getfield(L1, t, tempstring);
    }
    else if EQ("setfield") {
      int t;
      { GLOBAL_CHANGE(L); t = getindex; tempstring = getstring; }
      lua_setfield(L1, t, tempstring);
    }
    else if EQ("rawgeti") {
      int t;
      {
        GLOBAL_CHANGE(L);
        t = getindex_aux(L, L1, &pc);
        tempnum = getnum;
      }
      if(t == LUA_REGISTRYINDEX) {
        LuaValue val = L1->l_G->getRegistry()->get( LuaValue(tempnum) );
        L1->stack_.push(val);
      }
      else {
        lua_rawgeti(L1, t, tempnum);
      }
    }
    else if EQ("settable") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_settable(L1, tempindex);
    }
    else if EQ("setglobal") {
      { GLOBAL_CHANGE(L); tempstring = getstring; }
      lua_setglobal(L1, tempstring);
    }
    else if EQ("next") {
      lua_next(L1, -2);
    }
    else if EQ("concat") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_concat(L1, tempnum);
    }
    else if EQ("print") {
      int n;
      { GLOBAL_CHANGE(L); n = getnum; }
      if (n != 0) {
        printf("%s\n", luaL_tolstring(L1, n, NULL));
        L1->stack_.pop();
      }
      else {
        int i;
        n = L1->stack_.getTopIndex();
        for (i = 1; i <= n; i++) {
          printf("%s  ", luaL_tolstring(L1, i, NULL));
          L1->stack_.pop();
        }
        printf("\n");
      }
    }
    else if EQ("arith") {
      static char ops[] = "+-*/%^_";
      int op;
      skip(&pc);
      op = (int)(strchr(ops, *pc++) - ops);
      lua_arith(L1, op);
    }
    else if EQ("compare") {
      int a, b;
      { GLOBAL_CHANGE(L); a = getindex; b = getindex; tempnum = getnum; }
      int result = lua_compare(L1, a, b, tempnum);
      lua_pushboolean(L1, result);
    }
    else if EQ("call") {
      int narg, nres;
      { GLOBAL_CHANGE(L); narg = getnum; nres = getnum; }
      lua_call(L1, narg, nres);
    }
    else if EQ("pcall") {
      int narg, nres;
      { GLOBAL_CHANGE(L); narg = getnum; nres = getnum; }
      status = lua_pcall(L1, narg, nres, 0);
    }
    else if EQ("pcallk") {
      int narg, nres, i;
      { GLOBAL_CHANGE(L); narg = getnum; nres = getnum; i = getindex;}
      status = lua_pcallk(L1, narg, nres, 0, i, Cfunck);
    }
    else if EQ("callk") {
      int narg, nres, i;
      { GLOBAL_CHANGE(L); narg = getnum; nres = getnum; i = getindex;}
      lua_callk(L1, narg, nres, i, Cfunck);
    }
    else if EQ("yield") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      return lua_yield(L1, tempnum);
    }
    else if EQ("yieldk") {
      int nres, i;
      { GLOBAL_CHANGE(L); nres = getnum; i = getindex; }
      return lua_yieldk(L1, nres, i, Cfunck);
    }
    else if EQ("newthread") {
      lua_newthread(L1);
    }
    else if EQ("resume") {
      int i;
      { GLOBAL_CHANGE(L); i = getindex; tempnum = getnum; }
      LuaThread* tempthread = lua_tothread(L1, i);
      {
        GLOBAL_CHANGE(tempthread);
        status = lua_resume(tempthread, L, tempnum);
      }
    }
    else if EQ("pushstatus") {
      pushcode(L1, status);
    }
    else if EQ("xmove") {
      int f, t, n;
      { GLOBAL_CHANGE(L); f = getindex; t = getindex; n = getnum; }
      LuaThread *fs = (f == 0) ? L1 : lua_tothread(L1, f);
      LuaThread *ts = (t == 0) ? L1 : lua_tothread(L1, t);
      if (n == 0) {
        GLOBAL_CHANGE(fs);
        n = fs->stack_.getTopIndex();
      }
      {
        GLOBAL_CHANGE(fs);
        lua_xmove(fs, ts, n);
      }
    }
    else if EQ("loadstring") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      size_t sl;
      const char *s = luaL_checklstring(L1, tempnum, &sl);
      luaL_loadbuffer(L1, s, sl, s);
    }
    else if EQ("loadfile") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      luaL_loadfile(L1, luaL_checkstring(L1, tempnum));
    }
    else if EQ("setmetatable") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      lua_setmetatable(L1, tempindex);
    }
    else if EQ("getmetatable") {
      { GLOBAL_CHANGE(L); tempindex = getindex; }
      if (lua_getmetatable(L1, tempindex) == 0)
        L1->stack_.push(LuaValue::Nil());
    }
    else if EQ("type") {
      { GLOBAL_CHANGE(L); tempnum = getnum; }
      lua_pushstring(L1, luaL_typename(L1, tempnum));
    }
    else if EQ("append") {
      int t;
      { GLOBAL_CHANGE(L); t = getindex; }
      int i = (int)lua_rawlen(L1, t);
      lua_rawseti(L1, t, i + 1);
    }
    else if EQ("getctx") {
      int i = 0;
      int s = lua_getctx(L1, &i);
      pushcode(L1, s);
      lua_pushinteger(L1, i);
    }
    else if EQ("checkstack") {
      int sz;
      { GLOBAL_CHANGE(L); sz = getnum; tempstring = getstring; }
      luaL_checkstack(L1, sz, tempstring);
    }
    else if EQ("newmetatable") {
      { GLOBAL_CHANGE(L); tempstring = getstring; }
      LuaTable* registry = L1->l_G->getRegistry();
      LuaValue oldTable = registry->get(tempstring);

      int result = 0;
      if(oldTable.isTable()) {
        L1->stack_.push(oldTable);
        result = 0;
      }
      else {
        LuaTable* newTable = new LuaTable();
        registry->set(tempstring, newTable);
        L1->stack_.push(newTable);
        result = 1;
      }

      lua_pushboolean(L1, result);
    }
    else if EQ("testudata") {
      int i;
      { GLOBAL_CHANGE(L); i = getindex; tempstring = getstring; }
      lua_pushboolean(L1, luaL_testudata(L1, i, tempstring) != NULL);
    }
    else if EQ("gsub") {
      int a, b, c;
      { GLOBAL_CHANGE(L); a = getnum; b = getnum; c = getnum; }
      luaL_gsub(L1, lua_tostring(L1, a),
                    lua_tostring(L1, b),
                    lua_tostring(L1, c));
    }
    else if EQ("sethook") {
      int mask, count;
      { GLOBAL_CHANGE(L); mask = getnum; count = getnum; tempstring = getstring; }
      sethookaux(L1, mask, count, tempstring);
    }
    else if EQ("throw") {
      LuaResult error = (LuaResult)1;
      throwError(error);
    }
    else {
      GLOBAL_CHANGE(L);
      luaL_error(L, "unknown instruction %s", buff);
    }
  }
}


static int testC (LuaThread *L) {
  THREAD_CHECK(L);
  LuaThread *L1;
  const char *pc;
  if (lua_isuserdata(L, 1)) {
    L1 = (LuaThread *)L->stack_.at(1).getPointer();
    pc = luaL_checkstring(L, 2);
  }
  else if (lua_isthread(L, 1)) {
    L1 = lua_tothread(L, 1);
    pc = luaL_checkstring(L, 2);
  }
  else {
    L1 = L;
    pc = luaL_checkstring(L, 1);
  }
  return runC(L, L1, pc);
}


static int Cfunc (LuaThread *L) {
  THREAD_CHECK(L);
  return runC(L, L, lua_tostring(L, lua_upvalueindex(1)));
}


static int Cfunck (LuaThread *L) {
  THREAD_CHECK(L);
  int i = 0;
  lua_getctx(L, &i);
  return runC(L, L, lua_tostring(L, i));
}


static int makeCfunc (LuaThread *L) {
  THREAD_CHECK(L);
  luaL_checkstring(L, 1);
  lua_pushcclosure(L, Cfunc, L->stack_.getTopIndex());
  return 1;
}


/* }====================================================== */


/*
** {======================================================
** tests for C hooks
** =======================================================
*/

/*
** C hook that runs the C script stored in registry.C_HOOK[L]
*/
static void Chook (LuaThread *L, LuaDebug *ar) {
  THREAD_CHECK(L);
  const char *scpt;
  const char *const events [] = {"call", "ret", "line", "count", "tailcall"};
  lua_getregistryfield(L, "C_HOOK");

  L->stack_.push( LuaValue::Pointer(L) );

  lua_gettable(L, -2);  /* get C_HOOK[L] (script saved by sethookaux) */
  scpt = lua_tostring(L, -1);  /* not very religious (string will be popped) */
  L->stack_.pop(2);  /* remove C_HOOK and script */
  lua_pushstring(L, events[ar->event]);  /* may be used by script */
  lua_pushinteger(L, ar->currentline);  /* may be used by script */
  runC(L, L, scpt);  /* run script from C_HOOK[L] */
}


/*
** sets registry.C_HOOK[L] = scpt and sets Chook as a hook
*/
static void sethookaux (LuaThread *L, int mask, int count, const char *scpt) {
  THREAD_CHECK(L);
  if (*scpt == '\0') {  /* no script? */
    lua_sethook(L, NULL, 0, 0);  /* turn off hooks */
    return;
  }
  lua_getregistryfield(L, "C_HOOK");  /* get C_HOOK table */
  if (!lua_istable(L, -1)) {  /* no hook table? */
    L->stack_.pop();  /* remove previous value */
    lua_createtable(L, 0, 0);  /* create new C_HOOK table */
    L->stack_.copy(-1);
    lua_setregistryfield(L, "C_HOOK");  /* register it */
  }

  L->stack_.push( LuaValue::Pointer(L) );

  lua_pushstring(L, scpt);
  lua_settable(L, -3);  /* C_HOOK[L] = script */
  lua_sethook(L, Chook, mask, count);
}


static int sethook (LuaThread *L) {
  THREAD_CHECK(L);
  if (lua_isnoneornil(L, 1))
    lua_sethook(L, NULL, 0, 0);  /* turn off hooks */
  else {
    const char *scpt = luaL_checkstring(L, 1);
    const char *smask = luaL_checkstring(L, 2);
    int count = luaL_optint(L, 3, 0);
    int mask = 0;
    if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
    if (strchr(smask, 'r')) mask |= LUA_MASKRET;
    if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
    if (count > 0) mask |= LUA_MASKCOUNT;
    sethookaux(L, mask, count, scpt);
  }
  return 0;
}


static int coresume (LuaThread *L) {
  THREAD_CHECK(L);
  int status;
  LuaThread *co = lua_tothread(L, 1);
  luaL_argcheck(L, co, 1, "coroutine expected");
  {
    THREAD_CHANGE(co);
    status = lua_resume(co, L, 0);
  }
  if (status != LUA_OK && status != LUA_YIELD) {
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lua_pushboolean(L, 1);
    return 1;
  }
}

/* }====================================================== */



static const struct luaL_Reg tests_funcs[] = {
  {"checkmemory", lua_checkmemory},
  {"closestate", closestate},
  {"d2s", d2s},
  {"doonnewstack", doonnewstack},
  {"doremote", doremote},
  {"gccolor", get_gccolor},
  {"gcstate", gc_state},
  {"getref", getref},
  {"int2fb", int2fb_aux},
  {"limits", get_limits},
  {"listcode", listcode},
  {"listk", listk},
  {"listlocals", listlocals},
  {"loadlib", loadlib},
  {"newstate", newstate},
  {"newuserdata", newuserdata},
  {"num2int", num2int},
  {"pushuserdata", pushuserdata},
  {"querystr", string_query},
  {"querytab", table_query},
  {"ref", tref},
  {"resume", coresume},
  {"s2d", s2d},
  {"sethook", sethook},
  {"stacklevel", stacklevel},
  {"testC", testC},
  {"makeCfunc", makeCfunc},
  {"totalmem", mem_query},
  {"udataval", udataval},
  {"unref", unref},
  {"upvalue", upvalue},
  {NULL, NULL}
};


static void checkfinalmem (void) {
  assert(l_memcontrol.mem_blocks == 0);
  assert(l_memcontrol.mem_total == 0);
}


int luaopen_test (LuaThread *L) {
  THREAD_CHECK(L);
  lua_atpanic(L, &tpanic);
  atexit(checkfinalmem);

  LuaTable* lib = new LuaTable();
  for(const luaL_Reg* cursor = tests_funcs; cursor->name; cursor++) {
    lib->set( cursor->name, LuaValue(cursor->func) );
  }

  L->stack_.push(lib);

  return 1;
}
