/*
** $Id: lvm.c,v 2.147 2011/12/07 14:43:55 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100


// Converts value to string in-place, returning 1 if successful.
int luaV_tostring (TValue* v) {

  if(v->isString()) return 1;

  if (v->isNumber()) {
    lua_Number n = v->getNumber();
    char s[LUAI_MAXNUMBER2STR];
    int l = lua_number2str(s, n);
    *v = luaS_newlstr(s, l);
    return 1;
  }

  return 0;
}


static void traceexec (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  uint8_t mask = L->hookmask;
  if ((mask & LUA_MASKCOUNT) && L->hookcount == 0) {
    L->hookcount = L->basehookcount;
    luaD_hook(L, LUA_HOOKCOUNT, -1);
  }
  if (mask & LUA_MASKLINE) {
    Proto *p = ci_func(ci)->proto_;
    int npc = pcRel(ci->savedpc, p);
    int newline = getfuncline(p, npc);
    if (npc == 0 ||  /* call linehook when enter a new function, */
        ci->savedpc <= L->oldpc ||  /* when jump back (loop), or when */
        newline != getfuncline(p, pcRel(L->oldpc, p)))  /* enter a new line */
      luaD_hook(L, LUA_HOOKLINE, newline);
  }
  L->oldpc = ci->savedpc;
  if (L->status == LUA_YIELD) {  /* did hook yield? */
    ci->savedpc--;  /* undo increment (resume will increment it again) */
    luaD_throw(LUA_YIELD);
  }
}


static void callTM (lua_State *L, const TValue *f, const TValue *p1,
                    const TValue *p2, TValue *p3, int hasres) {
  THREAD_CHECK(L);
  ptrdiff_t result = savestack(L, p3);
  L->top[0] = *f; // push function
  L->top++;
  L->top[0] = *p1; // 1st argument
  L->top++;
  L->top[0] = *p2; // 2nd argument
  L->top++;
  if (!hasres) { // no result? 'p3' is third argument
    L->top[0] = *p3;  // 3rd argument
    L->top++;
  }
  L->checkstack(0);
  /* metamethod may yield only when called from Lua code */
  luaD_call(L, L->top - (4 - hasres), hasres, isLua(L->ci_));
  if (hasres) {  /* if has result, move it to its place */
    p3 = restorestack(L, result);
    L->top--;
    *p3 = L->top[0];
  }
}

static void callTM0 (lua_State *L,
                     const TValue* func,
                     const TValue* arg1,
                     const TValue* arg2,
                     const TValue* arg3) {
  THREAD_CHECK(L);
  L->top[0] = *func;
  L->top[1] = *arg1;
  L->top[2] = *arg2;
  L->top[3] = *arg3;
  L->top += 4;
  /* metamethod may yield only when called from Lua code */
  luaD_call(L, L->top - 4, 0, isLua(L->ci_));
}

static void callTM1 (lua_State *L,
                     TValue func,
                     TValue arg1,
                     TValue arg2,
                     TValue arg3) {
  THREAD_CHECK(L);
  L->top[0] = func;
  L->top[1] = arg1;
  L->top[2] = arg2;
  L->top[3] = arg3;
  L->top += 4;
  /* metamethod may yield only when called from Lua code */
  luaD_call(L, L->top - 4, 0, isLua(L->ci_));
}


void luaV_gettable (lua_State *L, const TValue *source, TValue *key, StkId result) {
  THREAD_CHECK(L);
  TValue tagmethod;
  for (int loop = 0; loop < MAXTAGLOOP; loop++) {
    if(source == NULL) {
      result->clear();
      return;
    }

    if (source->isTable()) {
      Table* table = source->getTable();
      const TValue* value = luaH_get2(table, key);

      if (value && !value->isNil()) {
        *result = *value;
        return;
      }
    }

    // Table lookup failed. If there's no tag method, then either the search terminates
    // (if object is a table) or throws an error (if object is not a table)

    if(source->isTable()) {
      tagmethod = fasttm2(source->getTable()->metatable, TM_INDEX);
    } else {
      tagmethod = luaT_gettmbyobj2(*source, TM_INDEX);
    }

    if(tagmethod.isNone() || tagmethod.isNil()) {
      if(source->isTable()) {
        result->clear();
        return;
      } else {
        luaG_typeerror(source, "index");
        return;
      }
    }

    // If the tagmethod is a function, call it. If it's a table, redo the search in
    // the new table.

    if (tagmethod.isFunction()) {
      callTM(L, &tagmethod, source, key, result, 1);
      return;
    }

    if(tagmethod.isTable()) {
      source = &tagmethod;
      continue;
    }

    // Trying to use other things as the __index tagmethod is an error.
    luaG_typeerror(source, "invalid type in __index method");
  }
  luaG_runerror("loop in gettable");
}

// TODO(aappleby): The original version of luaV_settable needs to be enshrined
// as an example of baaaaaad code.

void luaV_settable (lua_State *L, const TValue *t2, TValue *key, StkId val) {
  THREAD_CHECK(L);
  int loop;
  TValue cursor = *t2;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (cursor.isTable()) {
      Table *h = cursor.getTable();
      TValue oldval = h->get(*key);

      /* if previous value is not nil, there must be a previous entry
         in the table; moreover, a metamethod has no relevance */
      if (!oldval.isNone() && !oldval.isNil()) {
        luaH_set2(h, *key, *val);
        luaC_barrierback(h, *val);
        return;
      }

      /* previous value is nil; must check the metamethod */
      TValue tagmethod = fasttm2(h->metatable, TM_NEWINDEX);
      if (tagmethod.isNil() || tagmethod.isNone()) {
        // no metamethod, add (key,val) to table
        luaH_set2(h, *key, *val);
        luaC_barrierback(h, *val);
        return;
      }
      else {
        if (tagmethod.isFunction()) {
          callTM1(L, tagmethod, cursor, *key, *val);
          return;
        }
        else {
          cursor = tagmethod;
          continue;
        }
      }
    }
    else {
      /* not a table; check metamethod */
      TValue tagmethod = luaT_gettmbyobj2(cursor, TM_NEWINDEX);
      if (tagmethod.isNone() || tagmethod.isNil()) {
        luaG_typeerror(&cursor, "index");
      }
      else if (tagmethod.isFunction()) {
        callTM1(L, tagmethod, cursor, *key, *val);
        return;
      }
      else {
        cursor = tagmethod;
        continue;
      }
    }
  }
  luaG_runerror("loop in settable");
}


static int call_binTM (lua_State *L, const TValue *p1, const TValue *p2,
                       StkId res, TMS event) {
  THREAD_CHECK(L);
  TValue tm = luaT_gettmbyobj2(*p1, event);  /* try first operand */
  if (tm.isNone() || tm.isNil())
    tm = luaT_gettmbyobj2(*p2, event);  /* try second operand */
  if (tm.isNone() || tm.isNil()) return 0;
  callTM(L, &tm, p1, p2, res, 1);
  return 1;
}


TValue get_equalTM (lua_State *L, Table *mt1, Table *mt2,
                                  TMS event) {
  THREAD_CHECK(L);
  TValue tm1 = fasttm2(mt1, event);
  if (tm1.isNone() || tm1.isNil()) return TValue::None();  /* no metamethod */
  if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
  TValue tm2 = fasttm2(mt2, event);
  if (tm2.isNone() || tm2.isNil()) return TValue::None();  /* no metamethod */
  if (tm1 == tm2)  /* same metamethods? */
    return tm1;
  return TValue::None();
}


static int call_orderTM (lua_State *L, const TValue *p1, const TValue *p2,
                         TMS event) {
  THREAD_CHECK(L);
  if (!call_binTM(L, p1, p2, L->top, event))
    return -1;  /* no metamethod */
  else
    return !l_isfalse(L->top);
}


static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = ls->c_str();
  size_t ll = ls->getLen();
  const char *r = rs->c_str();
  size_t lr = rs->getLen();
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* strings are equal up to a `\0' */
      size_t len = strlen(l);  /* index of first `\0' in both strings */
      if (len == lr)  /* r is finished? */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l is finished? */
        return -1;  /* l is smaller than r (because r is not finished) */
      /* both strings longer than `len'; go on comparing (after the `\0') */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  THREAD_CHECK(L);
  int res;
  if (l->isNumber() && r->isNumber())
    return luai_numlt(L, l->getNumber(), r->getNumber());
  else if (l->isString() && r->isString())
    return l_strcmp(l->getString(), r->getString()) < 0;
  else if ((res = call_orderTM(L, l, r, TM_LT)) < 0)
    luaG_ordererror(l, r);
  return res;
}


int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r) {
  THREAD_CHECK(L);
  int res;
  if (l->isNumber() && r->isNumber())
    return luai_numle(L, l->getNumber(), r->getNumber());
  else if (l->isString() && r->isString())
    return l_strcmp(l->getString(), r->getString()) <= 0;
  else if ((res = call_orderTM(L, l, r, TM_LE)) >= 0)  /* first try `le' */
    return res;
  else if ((res = call_orderTM(L, r, l, TM_LT)) < 0)  /* else try `lt' */
    luaG_ordererror(l, r);
  return !res;
}


/*
** equality of Lua values. L == NULL means raw equality (no metamethods)
*/
int luaV_equalobj_ (lua_State *L, const TValue *t1, const TValue *t2) {
  THREAD_CHECK(L);
  if(t1->type() != t2->type()) return false;

  switch (t1->type()) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: {
      // note - if you compare raw bytes, this comparison fails for positive and
      // negative zero.
      return t1->getNumber() == t2->getNumber();
    }
    case LUA_TBOOLEAN: return t1->getBool() == t2->getBool();  /* true must be 1 !! */
    case LUA_TLIGHTUSERDATA: return t1->getLightUserdata() == t2->getLightUserdata();
    case LUA_TLCF: return t1->getLightFunction() == t2->getLightFunction();
    case LUA_TSTRING: return t1->getString() == t2->getString();

    case LUA_TUSERDATA: {
      if (t1->getUserdata() == t2->getUserdata()) return 1;
      if (L == NULL) return 0;

      TValue tm = get_equalTM(L, t1->getUserdata()->metatable_, t2->getUserdata()->metatable_, TM_EQ);
      if (tm.isNone() || tm.isNil()) return 0;  /* no TM? */

      callTM(L, &tm, t1, t2, L->top, 1);  /* call TM */
      return !l_isfalse(L->top);
    }

    case LUA_TTABLE: {
      if (t1->getTable() == t2->getTable()) return 1;
      if (L == NULL) return 0;

      TValue tm = get_equalTM(L, t1->getTable()->metatable, t2->getTable()->metatable, TM_EQ);
      if (tm.isNone() || tm.isNil()) return 0;  /* no TM? */

      callTM(L, &tm, t1, t2, L->top, 1);  /* call TM */
      return !l_isfalse(L->top);
    }

    default:
      assert(t1->isCollectable());
      return t1->getObject() == t2->getObject();
  }
}

// TODO(aappleby): Gaaaaah the logic in this is convoluted.
// Having code with side effects (tostring) in conditionals doesn't help.

void luaV_concat (lua_State *L, int total) {
  THREAD_CHECK(L);
  assert(total >= 2);
  do {
    StkId top = L->top;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(top[-2].isString() || top[-2].isNumber()) || !luaV_tostring(top-1)) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
        luaG_concaterror(top-2, top-1);
    }
    else if (top[-1].getString()->getLen() == 0) { /* second operand is empty? */
      luaV_tostring(top-2);
    }
    else if (top[-2].isString() && top[-2].getString()->getLen() == 0) {
      top[-2] = top[-1].getString();
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = top[-1].getString()->getLen();
      char *buffer;
      int i;
      /* collect total length */
      for (i = 1; i < total; i++) {
        TValue temp = top[-i-1].convertToString();
        if(temp.isNone()) break;
        top[-i-1] = temp;

        size_t l = top[-i-1].getString()->getLen();
        if (l >= (MAX_SIZET/sizeof(char)) - tl)
          luaG_runerror("string length overflow");
        tl += l;
      }
      buffer = luaZ_openspace(L, &G(L)->buff, tl);
      tl = 0;
      n = i;
      do {  /* concat all strings */
        size_t l = top[-i].getString()->getLen();
        memcpy(buffer+tl, top[-i].getString()->c_str(), l * sizeof(char));
        tl += l;
      } while (--i > 0);
      top[-n] = luaS_newlstr(buffer, tl);
    }
    total -= n-1;  /* got 'n' strings to create 1 new */
    L->top -= n-1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  THREAD_CHECK(L);

  if(rb->isString()) {
    ra[0] = rb->getString()->getLen();
    return;
  }

  TValue tagmethod = luaT_gettmbyobj2(*rb, TM_LEN);
  if (!tagmethod.isNone()) {
    callTM(L, &tagmethod, rb, rb, ra, 1);
    return;
  }

  if(rb->isTable()) {
    ra[0] = rb->getTable()->getLength();
    return;
  }

  luaG_typeerror(rb, "get length of");
  return;
}


void luaV_arith (lua_State *L, StkId ra, const TValue *rb,
                 const TValue *rc, TMS op) {
  THREAD_CHECK(L);

  TValue nb = rb->convertToNumber();

  if(nb.isNone()) {
    if (!call_binTM(L, rb, rc, ra, op)) {
      luaG_typeerror(rb, "perform arithmetic on");
    }
    return;
  }

  TValue nc = rc->convertToNumber();

  if(nc.isNone()) {
    if (!call_binTM(L, rb, rc, ra, op)) {
      luaG_typeerror(rc, "perform arithmetic on");
    }
    return;
  }

  int arithop = op - TM_ADD + LUA_OPADD;
  lua_Number res = luaO_arith(arithop, nb.getNumber(), nc.getNumber());
  *ra = res;
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static Closure *getcached (Proto *p, UpVal **encup, StkId base) {
  Closure *c = p->cache;
  if (c != NULL) {  /* is there a cached closure? */
    int nup = (int)p->upvalues.size();
    Upvaldesc *uv = p->upvalues.begin();
    int i;
    for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
      TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
      if (c->ppupvals_[i]->v != v)
        return NULL;  /* wrong upvalue; cannot reuse closure */
    }
  }
  return c;  /* return cached closure (or NULL if no cached closure) */
}


/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues. Note that the call to 'luaC_barrierproto' must come
** before the assignment to 'p->cache', as the function needs the
** original value of that field.
*/
static void pushclosure (lua_State *L,
                         Proto *p,
                         UpVal **encup,
                         StkId base,
                         StkId ra) {
  THREAD_CHECK(L);

  Closure *ncl = luaF_newLclosure(p);
  if(ncl == NULL) luaD_throw(LUA_ERRMEM);

  *ra = TValue::LClosure(ncl);  /* anchor new closure in stack */
  for (int i = 0; i < (int)p->upvalues.size(); i++) {  /* fill in its upvalues */
    if (p->upvalues[i].instack) {
      /* upvalue refers to local variable? */
      ncl->ppupvals_[i] = luaF_findupval(base + p->upvalues[i].idx);
    }
    else {
      /* get upvalue from enclosing function */
      ncl->ppupvals_[i] = encup[p->upvalues[i].idx];
    }
  }

  luaC_barrierproto(p, ncl);
  p->cache = ncl;  /* save it on cache for reuse */
}


/*
** finish execution of an opcode interrupted by an yield
*/
void luaV_finishOp (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  StkId base = ci->base;
  Instruction inst = *(ci->savedpc - 1);  /* interrupted instruction */
  OpCode op = GET_OPCODE(inst);
  switch (op) {  /* finish its execution */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_POW: case OP_UNM: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
      L->top--;
      base[GETARG_A(inst)] = L->top[0];
      break;
    }
    case OP_LE: case OP_LT: case OP_EQ: {
      int res = !l_isfalse(L->top - 1);
      L->top--;
      /* metamethod should not be called when operand is K */
      assert(!ISK(GETARG_B(inst)));
      /* "<=" using "<" instead? */
      TValue tm = luaT_gettmbyobj2(base[GETARG_B(inst)], TM_LE);
      if (op == OP_LE && (tm.isNone() || tm.isNil())) {
        res = !res;  /* invert result */
      }
      assert(GET_OPCODE(*ci->savedpc) == OP_JMP);
      if (res != GETARG_A(inst)) {
        /* condition failed? */
        ci->savedpc++;  /* skip jump instruction */
      }
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top - 1;  /* top when 'call_binTM' was called */
      int b = GETARG_B(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
      top[-2] = top[0];  /* put TM result in proper position */
      if (total > 1) {  /* are there elements to concat? */
        L->top = top - 1;  /* top is one after last element (at top-2) */
        luaV_concat(L, total);  /* concat them (may yield again) */
      }
      /* move final result to final position */
      ci->base[GETARG_A(inst)] = L->top[-1];
      L->top = ci->top;  /* restore top */
      break;
    }
    case OP_TFORCALL: {
      assert(GET_OPCODE(*ci->savedpc) == OP_TFORLOOP);
      L->top = ci->top;  /* correct top */
      break;
    }
    case OP_CALL: {
      if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
        L->top = ci->top;  /* adjust results */
      break;
    }
    case OP_TAILCALL: case OP_SETTABUP:  case OP_SETTABLE:
      break;
    default: assert(0);
  }
}



/*
** some macros for common tasks in `luaV_execute'
*/

#if !defined luai_runtimecheck
#define luai_runtimecheck(L, c)		/* void */
#endif


#define RA(i)	(base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))
#define KBx(i)  \
  (k + (GETARG_Bx(i) != 0 ? GETARG_Bx(i) - 1 : GETARG_Ax(*ci->savedpc++)))


/* execute a jump instruction */
#define dojump(ci,i,e) \
  { int a = GETARG_A(i); \
    if (a > 0) luaF_close(ci->base + a - 1); \
    ci->savedpc += GETARG_sBx(i) + e; }

/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ i = *ci->savedpc; dojump(ci, i, 1); }


#define Protect(x)	{ {x;}; base = ci->base; }

#define checkGC(L,c)	Protect(luaC_condGC(L, c);)


#define arith_op(op,tm) { \
        TValue *rb = RKB(i); \
        TValue *rc = RKC(i); \
        if (rb->isNumber() && rc->isNumber()) { \
          lua_Number nb = rb->getNumber(), nc = rc->getNumber(); \
          ra[0] = op(L, nb, nc); \
        } \
        else { Protect(luaV_arith(L, ra, rb, rc, tm)); } }


#define vmdispatch(o)	switch(o)
#define vmcase(l,b)	case l: {b}  break;
#define vmcasenb(l,b)	case l: {b}		/* nb = no break */

void luaV_execute (lua_State *L) {
  THREAD_CHECK(L);
  CallInfo *ci = L->ci_;
  Closure *cl;
  TValue *k;
  StkId base;
 newframe:  /* reentry point when frame changes (call/return) */
  assert(ci == L->ci_);
  cl = ci->func->getLClosure();
  k = cl->proto_->constants.begin();
  base = ci->base;
  /* main loop of interpreter */
  for (;;) {
    Instruction i = *(ci->savedpc++);
    StkId ra;
    if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
        (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {
      Protect(traceexec(L));
    }
    /* WARNING: several calls may realloc the stack and invalidate `ra' */
    ra = RA(i);
    assert(base == ci->base);
    assert(base <= L->top && L->top < L->stack.end());
    switch(GET_OPCODE(i)) {
      vmcase(OP_MOVE,
        *ra = *RB(i);
      )
      vmcase(OP_LOADK,
        *ra = k[GETARG_Bx(i)];
      )
      vmcase(OP_LOADKX,
        assert(GET_OPCODE(*ci->savedpc) == OP_EXTRAARG);
        *ra = k[GETARG_Ax(*ci->savedpc)];
        ci->savedpc++;
      )
      vmcase(OP_LOADBOOL,
        ra[0] = GETARG_B(i) ? true : false;
        if (GETARG_C(i)) ci->savedpc++;  /* skip next instruction (if C) */
      )
      vmcase(OP_LOADNIL,
        int b = GETARG_B(i);
        do {
          *ra = TValue::nil;
          ra++;
        } while (b--);
      )
      vmcase(OP_GETUPVAL,
        int b = GETARG_B(i);
        *ra = *cl->ppupvals_[b]->v;
      )
      vmcase(OP_GETTABUP,
        int b = GETARG_B(i);
        Protect(luaV_gettable(L, cl->ppupvals_[b]->v, RKC(i), ra));
      )
      vmcase(OP_GETTABLE,
        Protect(luaV_gettable(L, RB(i), RKC(i), ra));
      )
      vmcase(OP_SETTABUP,
        int a = GETARG_A(i);
        Protect(luaV_settable(L, cl->ppupvals_[a]->v, RKB(i), RKC(i)));
      )
      vmcase(OP_SETUPVAL,
        UpVal *uv = cl->ppupvals_[GETARG_B(i)];
        *uv->v = *ra;
        luaC_barrier(uv, *ra);
      )
      vmcase(OP_SETTABLE,
        Protect(luaV_settable(L, ra, RKB(i), RKC(i)));
      )
      vmcase(OP_NEWTABLE,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        Table *t = new Table();
        if(t == NULL) luaD_throw(LUA_ERRMEM);
        t->linkGC(getGlobalGCHead());
        *ra = t;
        if (b != 0 || c != 0)
          t->resize(luaO_fb2int(b), luaO_fb2int(c));
        checkGC(L,
          L->top = ra + 1;  /* limit of live values */
          luaC_step();
          L->top = ci->top;  /* restore top */
        )
      )
      vmcase(OP_SELF,
        StkId rb = RB(i);
        ra[1] = *rb;
        Protect(luaV_gettable(L, rb, RKC(i), ra));
      )
      case OP_ADD: { arith_op(luai_numadd, TM_ADD) }; break;
      vmcase(OP_SUB,
        arith_op(luai_numsub, TM_SUB);
      )
      vmcase(OP_MUL,
        arith_op(luai_nummul, TM_MUL);
      )
      vmcase(OP_DIV,
        arith_op(luai_numdiv, TM_DIV);
      )
      vmcase(OP_MOD,
        arith_op(luai_nummod, TM_MOD);
      )
      vmcase(OP_POW,
        arith_op(luai_numpow, TM_POW);
      )
      vmcase(OP_UNM,
        TValue *rb = RB(i);
        if (rb->isNumber()) {
          lua_Number nb = rb->getNumber();
          ra[0] = -nb;
        }
        else {
          Protect(luaV_arith(L, ra, rb, rb, TM_UNM));
        }
      )
      vmcase(OP_NOT,
        TValue *rb = RB(i);
        int res = l_isfalse(rb);  /* next assignment may change this value */
        ra[0] = res ? true : false;
      )
      vmcase(OP_LEN,
        Protect(luaV_objlen(L, ra, RB(i)));
      )
      vmcase(OP_CONCAT,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        StkId rb;
        L->top = base + c + 1;  /* mark the end of concat operands */
        Protect(luaV_concat(L, c - b + 1));
        ra = RA(i);  /* 'luav_concat' may invoke TMs and move the stack */
        rb = b + base;
        *ra = b[base];
        checkGC(L,
          L->top = (ra >= rb ? ra + 1 : rb);  /* limit of live values */
          luaC_step();
        )
        L->top = ci->top;  /* restore top */
      )
      vmcase(OP_JMP,
        dojump(ci, i, 0);
      )
      vmcase(OP_EQ,
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        Protect(
          if (cast_int(luaV_equalobj_(L, rb, rc)) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_LT,
        Protect(
          if (luaV_lessthan(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_LE,
        Protect(
          if (luaV_lessequal(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_TEST,
        if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
            ci->savedpc++;
          else
          donextjump(ci);
      )
      vmcase(OP_TESTSET,
        TValue *rb = RB(i);
        if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
          ci->savedpc++;
        else {
          *ra = *rb;
          donextjump(ci);
        }
      )
      vmcase(OP_CALL,
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        if (luaD_precall(L, ra, nresults)) {  /* C function? */
          if (nresults >= 0) L->top = ci->top;  /* adjust results */
          base = ci->base;
        }
        else {  /* Lua function */
          ci = L->ci_;
          ci->callstatus |= CIST_REENTRY;
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcase(OP_TAILCALL,
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        assert(GETARG_C(i) - 1 == LUA_MULTRET);
        if (luaD_precall(L, ra, LUA_MULTRET))  /* C function? */
          base = ci->base;
        else {
          /* tail call: put called frame (n) in place of caller one (o) */
          CallInfo *nci = L->ci_;  /* called frame */
          CallInfo *oci = nci->previous;  /* caller frame */
          StkId nfunc = nci->func;  /* called function */
          StkId ofunc = oci->func;  /* caller function */
          /* last stack slot filled by 'precall' */
          StkId lim = nci->base + nfunc->getLClosure()->proto_->numparams;
          int aux;
          /* close all upvalues from previous call */
          if (cl->proto_->subprotos_.size() > 0) luaF_close(oci->base);
          /* move new frame into old one */
          for (aux = 0; nfunc + aux < lim; aux++) {
            ofunc[aux] = nfunc[aux];
          }
          oci->base = ofunc + (nci->base - nfunc);  /* correct base */
          oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
          oci->savedpc = nci->savedpc;
          oci->callstatus |= CIST_TAIL;  /* function was tail called */
          ci = L->ci_ = oci;  /* remove new frame */
          assert(L->top == oci->base + ofunc->getLClosure()->proto_->maxstacksize);
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcasenb(OP_RETURN,
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b-1;
        if (cl->proto_->subprotos_.size() > 0) luaF_close(base);
        b = luaD_poscall(L, ra);
        if (!(ci->callstatus & CIST_REENTRY))  /* 'ci' still the called one */
          return;  /* external invocation: return */
        else {  /* invocation via reentry: continue execution */
          ci = L->ci_;
          if (b) L->top = ci->top;
          assert(isLua(ci));
          assert(GET_OPCODE(*((ci)->savedpc - 1)) == OP_CALL);
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcase(OP_FORLOOP,
        lua_Number step = ra[2].getNumber();
        lua_Number idx = ra[0].getNumber() + step; /* increment index */
        lua_Number limit = ra[1].getNumber();
        if (luai_numlt(L, 0, step) ? luai_numle(L, idx, limit)
                                   : luai_numle(L, limit, idx)) {
          ci->savedpc += GETARG_sBx(i);  /* jump back */
          ra[0] = idx;  /* update internal index... */
          ra[3] = idx;  /* ...and external index */
        }
      )
      vmcase(OP_FORPREP,
        TValue init = ra[0].convertToNumber();
        TValue plimit = ra[1].convertToNumber();
        TValue pstep = ra[2].convertToNumber();

        if (init.isNone())   luaG_runerror(LUA_QL("for") " initial value must be a number");
        if (plimit.isNone()) luaG_runerror(LUA_QL("for") " limit must be a number");
        if (pstep.isNone())  luaG_runerror(LUA_QL("for") " step must be a number");

        ra[0] = init.getNumber() - pstep.getNumber();
        ra[1] = plimit;
        ra[2] = pstep;

        ci->savedpc += GETARG_sBx(i);
      )
      vmcasenb(OP_TFORCALL,
        StkId cb = ra + 3;  /* call base */
        cb[2] = ra[2];
        cb[1] = ra[1];
        cb[0] = ra[0];
        L->top = cb + 3;  /* func. + 2 args (state and index) */
        Protect(luaD_call(L, cb, GETARG_C(i), 1));
        L->top = ci->top;
        i = *(ci->savedpc++);  /* go to next instruction */
        ra = RA(i);
        assert(GET_OPCODE(i) == OP_TFORLOOP);
        goto l_tforloop;
      )
      vmcase(OP_TFORLOOP,
        l_tforloop:
        if (ra[1].isNotNil()) {  /* continue loop? */
          ra[0] = ra[1];  /* save control variable */
          ci->savedpc += GETARG_sBx(i);  /* jump back */
        }
      )
      vmcase(OP_SETLIST,
        int n = GETARG_B(i);
        int c = GETARG_C(i);
        int last;
        Table *h;
        if (n == 0) n = cast_int(L->top - ra) - 1;
        if (c == 0) {
          assert(GET_OPCODE(*ci->savedpc) == OP_EXTRAARG);
          c = GETARG_Ax(*ci->savedpc++);
        }
        luai_runtimecheck(L, ra->isTable());
        h = ra->getTable();
        last = ((c-1)*LFIELDS_PER_FLUSH) + n;
        
        // needs more space? pre-allocate it at once.
        if (last > (int)h->array.size()) {
          h->resize(last, (int)h->hashtable.size());
        }

        for (; n > 0; n--) {
          TValue *val = ra+n;
          luaH_setint(h, last--, val);
          // TODO(aappleby): we probably don't have to call barrierback every time through this loop
          luaC_barrierback(h, *val);
        }
        L->top = ci->top;  /* correct top (in case of previous open call) */
      )
      vmcase(OP_CLOSURE,
        Proto *p = cl->proto_->subprotos_[GETARG_Bx(i)];
        Closure *ncl = getcached(p, cl->ppupvals_, base);  /* cached closure */
        if (ncl == NULL)  /* no match? */
          pushclosure(L, p, cl->ppupvals_, base, ra);  /* create a new one */
        else
          *ra = TValue::LClosure(ncl);  /* push cashed closure */
        checkGC(L,
          L->top = ra + 1;  /* limit of live values */
          luaC_step();
          L->top = ci->top;  /* restore top */
        )
      )
      vmcase(OP_VARARG,
        int b = GETARG_B(i) - 1;
        int j;
        int n = cast_int(base - ci->func) - cl->proto_->numparams - 1;
        if (b < 0) {  /* B == 0? */
          b = n;  /* get all var. arguments */
          L->checkstack(n);
          base = ci->base;
          ra = RA(i);  /* previous call may change the stack */
          L->top = ra + n;
        }
        for (j = 0; j < b; j++) {
          if (j < n) {
            ra[j] = base[j-n];
          }
          else {
            ra[j] = TValue::nil;
          }
        }
      )
      vmcase(OP_EXTRAARG,
        assert(0);
      )
    }
  }
}

