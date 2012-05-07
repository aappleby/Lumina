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
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "ltm.h"
#include "lvm.h"

/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100

// Converts value to string in-place, returning 1 if successful.
int luaV_tostring (LuaValue* v) {
  if(v->isString()) return 1;

  if (v->isNumber()) {
    double n = v->getNumber();
    char s[LUAI_MAXNUMBER2STR];
    int l = lua_number2str(s, n);
    *v = thread_G->strings_->Create(s, l);
    return 1;
  }

  return 0;
}


static void callTM (LuaThread *L, const LuaValue *f, const LuaValue *p1,
                    const LuaValue *p2, LuaValue *p3, int hasres) {
  THREAD_CHECK(L);
  ptrdiff_t result = L->stack_.indexOf(p3);
  L->stack_.push_nocheck(*f); // push function
  L->stack_.push_nocheck(*p1); // 1st argument
  L->stack_.push_nocheck(*p2); // 2nd argument
  if (!hasres) { // no result? 'p3' is third argument
    L->stack_.push_nocheck(*p3);  // 3rd argument
  }

  LuaResult result2 = L->stack_.reserve2(0);
  handleResult(result2);

  /* metamethod may yield only when called from Lua code */
  luaD_call(L, hasres ? 2 : 3, hasres, L->stack_.callinfo_->isLua());
  if (hasres) {  /* if has result, move it to its place */
    p3 = L->stack_.atIndex(result);
    *p3 = L->stack_.pop();
  }
}

static void callTM3 (LuaThread *L,
                     LuaValue f,
                     LuaValue p1,
                     LuaValue p2,
                     LuaValue& result) {
  THREAD_CHECK(L);

  L->stack_.push_nocheck(f); // push function
  L->stack_.push_nocheck(p1); // 1st argument
  L->stack_.push_nocheck(p2); // 2nd argument
  LuaResult result2 = L->stack_.reserve2(0);
  handleResult(result2);

  /* metamethod may yield only when called from Lua code */
  luaD_call(L, 2, 1, L->stack_.callinfo_->isLua());
  result = L->stack_.pop();
}

static void callTM1 (LuaThread *L,
                     LuaValue func,
                     LuaValue arg1,
                     LuaValue arg2,
                     LuaValue arg3) {
  THREAD_CHECK(L);

  L->stack_.push_nocheck(func);
  L->stack_.push_nocheck(arg1);
  L->stack_.push_nocheck(arg2);
  L->stack_.push_nocheck(arg3);
  LuaResult result = L->stack_.reserve2(0);
  handleResult(result);

  /* metamethod may yield only when called from Lua code */
  luaD_call(L, 3, 0, L->stack_.callinfo_->isLua());
}

LuaResult luaV_gettable2 (LuaThread *L, LuaValue source, LuaValue key, LuaValue& outResult) {
  THREAD_CHECK(L);
  LuaValue tagmethod;

  for (int loop = 0; loop < MAXTAGLOOP; loop++) {

    if (source.isTable()) {
      LuaTable* table = source.getTable();
      LuaValue value = table->get(key);

      if(!value.isNone() && !value.isNil()) {
        // Basic table lookup, nothing weird going on here.
        outResult = value;
        return LUA_OK;
      }
    }

    // LuaTable lookup failed. If there's no tag method, then either the search terminates
    // (if object is a table) or throws an error (if object is not a table)

    if(source.isTable()) {
      tagmethod = fasttm2(source.getTable()->metatable, TM_INDEX);
    } else {
      tagmethod = luaT_gettmbyobj2(source, TM_INDEX);
    }

    if(tagmethod.isNone() || tagmethod.isNil()) {
      if(source.isTable()) {
        outResult = LuaValue::Nil();
        return LUA_OK;
      } else {
        return LUA_BAD_TABLE;
      }
    }

    // If the tagmethod is a function, call it. If it's a table, redo the search in
    // the new table.

    if (tagmethod.isFunction()) {
      callTM3(L, tagmethod, source, key, outResult);
      return LUA_OK;
    }

    if(tagmethod.isTable()) {
      source = tagmethod;
      continue;
    }

    // Trying to use other things as the __index tagmethod is an error.
    return LUA_BAD_INDEX_TM;
  }

  return LUA_META_LOOP;
}

// TODO(aappleby) - This gets a StkId parameter, but the tag method calling can invalidate the stack.
// Very dangerous, need to replace.

void luaV_gettable (LuaThread *L, const LuaValue *source, LuaValue *key, StkId outResult) {
  THREAD_CHECK(L);

  int stackIndex = (int)(outResult - L->stack_.begin());

  LuaValue result;
  LuaResult r = luaV_gettable2(L, *source, *key, result);

  if(r == LUA_OK) {
    L->stack_[stackIndex] = result;
  } else {
    handleResult(r, source);
  }
}

// TODO(aappleby): The original version of luaV_settable needs to be enshrined
// as an example of baaaaaad code.

void luaV_settable (LuaThread *L, const LuaValue *t2, LuaValue *key, StkId val) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  int loop;
  LuaValue cursor = *t2;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (cursor.isTable()) {
      LuaTable *h = cursor.getTable();
      LuaValue oldval = h->get(*key);

      /* if previous value is not nil, there must be a previous entry
         in the table; moreover, a metamethod has no relevance */
      if (!oldval.isNone() && !oldval.isNil()) {
        if(key->isNil()) {
          result = luaG_runerror("Key is invalid (either nil or NaN)");
          handleResult(result);
        }
        if(key->isNone()) {
          result = luaG_runerror("Key is invalid (either nil or NaN)");
          handleResult(result);
        }
        if(key->isNumber()) {
          double n = key->getNumber();
          if(n != n) {
            result = luaG_runerror("Key is invalid (either nil or NaN)");
            handleResult(result);
          }
        }

        h->set(*key,*val);
        luaC_barrierback(h, *key);
        luaC_barrierback(h, *val);
        return;
      }

      /* previous value is nil; must check the metamethod */
      LuaValue tagmethod = fasttm2(h->metatable, TM_NEWINDEX);
      if (tagmethod.isNil() || tagmethod.isNone()) {
        // no metamethod, add (key,val) to table
        if(key->isNil()) {
          result = luaG_runerror("Key is invalid (either nil or NaN)");
          handleResult(result);
        }
        if(key->isNone()) {
          result = luaG_runerror("Key is invalid (either nil or NaN)");
          handleResult(result);
        }
        if(key->isNumber()) {
          double n = key->getNumber();
          if(n != n) {
            result = luaG_runerror("Key is invalid (either nil or NaN)");
            handleResult(result);
          }
        }

        h->set(*key,*val);
        luaC_barrierback(h, *key);
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
      LuaValue tagmethod = luaT_gettmbyobj2(cursor, TM_NEWINDEX);
      if (tagmethod.isNone() || tagmethod.isNil()) {
        result = luaG_typeerror(&cursor, "index");
        handleResult(result);
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
  result = luaG_runerror("loop in settable");
  handleResult(result);
}


static int call_binTM (LuaThread *L, const LuaValue *p1, const LuaValue *p2,
                       StkId res, TMS event) {
  THREAD_CHECK(L);
  LuaValue tm = luaT_gettmbyobj2(*p1, event);  /* try first operand */
  if (tm.isNone() || tm.isNil())
    tm = luaT_gettmbyobj2(*p2, event);  /* try second operand */
  if (tm.isNone() || tm.isNil()) return 0;
  callTM(L, &tm, p1, p2, res, 1);
  return 1;
}


LuaValue get_equalTM (LuaThread *L, LuaTable *mt1, LuaTable *mt2,
                                  TMS event) {
  THREAD_CHECK(L);
  LuaValue tm1 = fasttm2(mt1, event);
  if (tm1.isNone() || tm1.isNil()) return LuaValue::None();  /* no metamethod */
  if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
  LuaValue tm2 = fasttm2(mt2, event);
  if (tm2.isNone() || tm2.isNil()) return LuaValue::None();  /* no metamethod */
  if (tm1 == tm2)  /* same metamethods? */
    return tm1;
  return LuaValue::None();
}


static int call_orderTM (LuaThread *L, const LuaValue *p1, const LuaValue *p2,
                         TMS event) {
  THREAD_CHECK(L);
  if (!call_binTM(L, p1, p2, L->stack_.top_, event))
    return -1;  /* no metamethod */
  else
    return L->stack_.top_->isTrue();
}


static int l_strcmp (const LuaString *ls, const LuaString *rs) {
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


int luaV_lessthan (LuaThread *L, const LuaValue *l, const LuaValue *r) {
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


int luaV_lessequal (LuaThread *L, const LuaValue *l, const LuaValue *r) {
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
int luaV_equalobj_ (LuaThread *L, const LuaValue *t1, const LuaValue *t2) {
  THREAD_CHECK(L);
  if(t1->type() != t2->type()) {
    return 0;
  }

  if(t1->isNumber()) {
    return t1->getNumber() == t2->getNumber();
  }

  if(t1->getRawBytes() == t2->getRawBytes()) {
    return 1;
  }

  // Types match, raw bytes don't match. If the objects are tables or
  // userdata, try the tag methods.

  if(L == NULL) {
    assert(false);
    return 0;
  }

  if(t1->isBlob()) {
    LuaValue tm = get_equalTM(L, t1->getBlob()->metatable_, t2->getBlob()->metatable_, TM_EQ);

    if(tm.isFunction()) {
      callTM(L, &tm, t1, t2, L->stack_.top_, 1);  /* call TM */
      return L->stack_.top_->isTrue();
    }
  }

  if(t1->isTable()) {
    LuaValue tm = get_equalTM(L, t1->getTable()->metatable, t2->getTable()->metatable, TM_EQ);

    if(tm.isFunction()) {
      callTM(L, &tm, t1, t2, L->stack_.top_, 1);  /* call TM */
      return L->stack_.top_->isTrue();
    }
  }

  return 0;
}

// TODO(aappleby): Gaaaaah the logic in this is convoluted.
// Having code with side effects (tostring) in conditionals doesn't help.

void luaV_concat (LuaThread *L, int total) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  assert(total >= 2);
  do {
    StkId top = L->stack_.top_;
    int n = 2;  /* number of elements handled in this pass (at least 2) */

    if (!top[-2].isString() && !top[-2].isNumber()) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT)) {
        luaG_concaterror(top-2, top-1);
      }
      total -= n-1;  /* got 'n' strings to create 1 new */
      L->stack_.top_ -= n-1;  /* popped 'n' strings and pushed one */
      continue;
    }

    int tostring_result = 0;
    tostring_result = luaV_tostring(top-1);

    if (!tostring_result) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT)) {
        luaG_concaterror(top-2, top-1);
      }
      total -= n-1;  /* got 'n' strings to create 1 new */
      L->stack_.top_ -= n-1;  /* popped 'n' strings and pushed one */
      continue;
    }

    if (top[-1].getString()->getLen() == 0) { /* second operand is empty? */
      luaV_tostring(top-2);
      total -= n-1;  /* got 'n' strings to create 1 new */
      L->stack_.top_ -= n-1;  /* popped 'n' strings and pushed one */
      continue;
    }

    if (top[-2].isString() && top[-2].getString()->getLen() == 0) {
      top[-2] = top[-1].getString();
      total -= n-1;  /* got 'n' strings to create 1 new */
      L->stack_.top_ -= n-1;  /* popped 'n' strings and pushed one */
      continue;
    }

    /* at least two non-empty string values; get as many as possible */
    size_t tl = top[-1].getString()->getLen();
    char *buffer;
    int i;
    /* collect total length */
    for (i = 1; i < total; i++) {
      {
        LuaValue temp = top[-i-1].convertToString();
        if(temp.isNone()) break;
        top[-i-1] = temp;
      }

      size_t l = top[-i-1].getString()->getLen();
      if (l >= (MAX_SIZET/sizeof(char)) - tl) {
        result = luaG_runerror("string length overflow");
        handleResult(result);
      }
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

    top[-n] = thread_G->strings_->Create(buffer, tl);

    total -= n-1;  /* got 'n' strings to create 1 new */
    L->stack_.top_ -= n-1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


void luaV_objlen (LuaThread *L, StkId ra, const LuaValue *rb) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);

  if(rb->isString()) {
    ra[0] = rb->getString()->getLen();
    return;
  }

  LuaValue tagmethod = luaT_gettmbyobj2(*rb, TM_LEN);
  if (!tagmethod.isNone()) {
    callTM(L, &tagmethod, rb, rb, ra, 1);
    return;
  }

  if(rb->isTable()) {
    ra[0] = rb->getTable()->getLength();
    return;
  }

  result = luaG_typeerror(rb, "get length of");
  handleResult(result);
  return;
}


void luaV_arith (LuaThread *L, StkId ra, const LuaValue *rb, const LuaValue *rc, TMS op) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);

  LuaValue nb = rb->convertToNumber();

  if(nb.isNone()) {
    if (!call_binTM(L, rb, rc, ra, op)) {
      result = luaG_typeerror(rb, "perform arithmetic on");
      handleResult(result);
    }
    return;
  }

  LuaValue nc = rc->convertToNumber();

  if(nc.isNone()) {
    if (!call_binTM(L, rb, rc, ra, op)) {
      result = luaG_typeerror(rc, "perform arithmetic on");
      handleResult(result);
    }
    return;
  }

  int arithop = op - TM_ADD + LUA_OPADD;
  double res = luaO_arith(arithop, nb.getNumber(), nc.getNumber());
  *ra = res;
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static LuaClosure *getcached (LuaProto *p, LuaUpvalue **encup, StkId base) {
  LuaClosure *c = p->cache;
  if (c != NULL) {  /* is there a cached closure? */
    int nup = (int)p->upvalues.size();
    Upvaldesc *uv = p->upvalues.begin();
    int i;
    for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
      LuaValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
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
static void pushclosure (LuaThread *L,
                         LuaProto *p,
                         LuaUpvalue **encup,
                         StkId base,
                         StkId ra) {
  THREAD_CHECK(L);

  LuaClosure *ncl = new LuaClosure(p, (int)p->upvalues.size());

  *ra = LuaValue(ncl);  /* anchor new closure in stack */
  for (int i = 0; i < (int)p->upvalues.size(); i++) {  /* fill in its upvalues */
    if (p->upvalues[i].instack) {
      /* upvalue refers to local variable? */
      ncl->ppupvals_[i] = thread_L->stack_.createUpvalFor(base + p->upvalues[i].idx);
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
void luaV_finishOp (LuaThread *L) {
  THREAD_CHECK(L);
  LuaStackFrame *ci = L->stack_.callinfo_;
  StkId base = ci->getBase();

  Instruction inst = (Instruction)ci->getCurrentInstruction();  /* interrupted instruction */
  OpCode op = (OpCode)ci->getCurrentOp();
  switch (op) {  /* finish its execution */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_POW: case OP_UNM: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
      base[GETARG_A(inst)] = L->stack_.pop();
      break;
    }
    case OP_LE: case OP_LT: case OP_EQ: {
      int res = L->stack_.pop().isTrue();
      /* metamethod should not be called when operand is K */
      assert(!ISK(GETARG_B(inst)));
      /* "<=" using "<" instead? */
      LuaValue tm = luaT_gettmbyobj2(base[GETARG_B(inst)], TM_LE);
      if (op == OP_LE && (tm.isNone() || tm.isNil())) {
        res = !res;  /* invert result */
      }
      assert(ci->getNextOp() == OP_JMP);
      if (res != GETARG_A(inst)) {
        /* condition failed? */
        ci->Jump(1);  /* skip jump instruction */
      }
      break;
    }
    case OP_CONCAT: {
      StkId top = L->stack_.top_ - 1;  /* top when 'call_binTM' was called */
      int b = GETARG_B(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
      top[-2] = top[0];  /* put TM result in proper position */
      if (total > 1) {  /* are there elements to concat? */
        L->stack_.top_ = top - 1;  /* top is one after last element (at top-2) */
        luaV_concat(L, total);  /* concat them (may yield again) */
      }
      /* move final result to final position */
      ci->getBase()[GETARG_A(inst)] = L->stack_.top_[-1];
      L->stack_.top_ = ci->getTop();  /* restore top */
      break;
    }
    case OP_TFORCALL: {
      assert(ci->getNextOp() == OP_TFORLOOP);
      L->stack_.top_ = ci->getTop();  /* correct top */
      break;
    }
    case OP_CALL: {
      if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
        L->stack_.top_ = ci->getTop();  /* adjust results */
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

#define RA(i)	(base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	  check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	  check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))

#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))

enum RunResult {
  RR_DONE
};

RunResult luaV_run2 (LuaThread *L) {
  LuaResult result = LUA_OK;

  THREAD_CHECK(L);
  
  /* main loop of interpreter */
  for (int cycle = 0;; cycle++) {
    
    LuaStackFrame *ci = L->stack_.callinfo_;

    if(thread_G->getGCDebt() > 0) {
      luaC_step();
    }

    l_memcontrol.checkLimit();

    Instruction i = ci->beginInstruction();
    OpCode opcode = (OpCode)(i & 0x0000003F);

    uint32_t A  = (i >>  6) & 0x000000FF;
    uint32_t B  = (i >> 23) & 0x000001FF;
    uint32_t Bk = (i >> 23) & 0x000000FF;
    uint32_t C  = (i >> 14) & 0x000001FF;
    uint32_t Ck = (i >> 14) & 0x000000FF;
    //uint32_t Ax = (i >>  6) & 0x03FFFFFF;
    uint32_t Bx = (i >> 14) & 0x0003FFFF;
    //int32_t Bs = (int32_t)Bx - 0x1FFFF;

    // Various hook and debug calls expect that the program counter is incremented _before_
    // the instruction is executed. This is annoying.

    int mask = L->hookmask;
    if (mask & (LUA_MASKLINE | LUA_MASKCOUNT)) {
      L->hookcount--;

      if ((mask & LUA_MASKCOUNT) && L->hookcount == 0) {
        L->hookcount = L->basehookcount;
        luaD_hook(L, LUA_HOOKCOUNT, -1);
      }

      LuaProto *p = ci->getFunc()->getLClosure()->proto_;
      int npc = ci->getCurrentPC();
      int newline = ci->getCurrentLine();

      if (mask & LUA_MASKLINE) {
        if (npc == 0 ||  /* call linehook when enter a new function, */
            ci->getCurrentPC() <= L->oldpc ||  /* when jump back (loop), or when */
            newline != p->getLine(L->oldpc))  /* enter a new line */
          luaD_hook(L, LUA_HOOKLINE, newline);
      }

      L->oldpc = ci->getCurrentPC();

      if (L->status == LUA_YIELD) {  /* did hook yield? */
        /* undo increment (resume will increment it again) */
        ci->undoInstruction();
        throwError(LUA_YIELD);
      }
    }

    //assert(base <= L->stack_.top_ && L->stack_.top_ < L->stack_.end());
    LuaValue* base = ci->getBase();
    StkId ra = &base[A];
    LuaClosure* cl = ci->getFunc()->getLClosure();
    LuaValue* k = ci->getConstants();

    switch(opcode) {
      case OP_MOVE:
        {
          base[A] = base[B];
          break;
        }

      case OP_LOADK:
        {
          base[A] = k[Bx];
          break;
        }

      case OP_LOADKX:
        {
          assert(ci->getNextOp() == OP_EXTRAARG);
          base[A] = k[GETARG_Ax(ci->getNextInstruction())];
          ci->Jump(1);
          break;
        }

      case OP_LOADBOOL:
        {
          base[A] = B ? true : false;
          if (C) ci->Jump(1);  /* skip next instruction (if C) */
          break;
        }

      case OP_LOADNIL: 
        {
          for(uint32_t b = 0; b <= B; b++) {
            base[A+b] = LuaValue::Nil();
          }
          break;
        }

      case OP_GETUPVAL: 
        {
          base[A] = *cl->ppupvals_[B]->v;
          break;
        }

      case OP_GETTABUP:
        {
          luaV_gettable(L, cl->ppupvals_[B]->v, RKC(i), &base[A]); 
          break;
        }

      case OP_GETTABLE:
        {
          luaV_gettable(L, &base[B], RKC(i), &base[A]);
          break;
        }

      case OP_SETTABUP: 
        {
          int a = GETARG_A(i);
          luaV_settable(L, cl->ppupvals_[a]->v, RKB(i), RKC(i));
          break;
        }

      case OP_SETUPVAL:
        {
          LuaUpvalue *uv = cl->ppupvals_[GETARG_B(i)];
          *uv->v = base[A];
          luaC_barrier(uv, base[A]);
          break;
        }

      case OP_SETTABLE:
        {
          luaV_settable(L, &base[A], RKB(i), RKC(i));
          break;
        }

      case OP_NEWTABLE:
        {
          int b = luaO_fb2int( GETARG_B(i) );
          int c = luaO_fb2int( GETARG_C(i) );

          LuaTable* t = new LuaTable(b, c);
          base[A] = t;

          break;
        }

      case OP_SELF:
        {
          base[A+1] = base[B];
          luaV_gettable(L,
                        &base[B],
                        (C & 256) ? &k[Ck] : &base[Ck],
                        &base[A]);
          break;
        }

      case OP_ADD:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = nb + nc;
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_ADD);
          }
          break;
        }

      case OP_SUB:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = nb - nc;
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_SUB);
          }
          break;
        }

      case OP_MUL:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = nb * nc;
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_MUL);
          }
          break;
        }

      case OP_DIV:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = nb / nc;
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_DIV);
          }
          break;
        }

      case OP_MOD:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = ((nb) - floor((nb)/(nc))*(nc));
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_MOD);
          }
          break;
        }

      case OP_POW:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (rb->isNumber() && rc->isNumber()) {
            double nb = rb->getNumber();
            double nc = rc->getNumber();
            base[A] = pow(nb,nc);
          } else {
            luaV_arith(L,
                       &base[A],
                       rb,
                       rc,
                       TM_POW);
          }
          break;
        }

      case OP_UNM:
        {
          if (base[B].isNumber()) {
            double nb = base[B].getNumber();
            base[A] = -nb;
          }
          else {
            luaV_arith(L,
                       &base[A],
                       &base[B],
                       &base[B],
                       TM_UNM);
          }
          break;
        }

      case OP_NOT:
        {
          base[A] = base[B].isFalse();
          break;
        }

      case OP_LEN:
        {
          luaV_objlen(L, &base[A], &base[B]);
          break;
        }

      case OP_CONCAT:
        {
          L->stack_.top_ = &base[C + 1];  /* mark the end of concat operands */
          luaV_concat(L, C - B + 1);

          // concat may realloc the stack
          base = ci->getBase();
          base[A] = base[B];

          L->stack_.top_ = ci->getTop();  /* restore top */
          break;
        }

      case OP_JMP:
        {
          if (A > 0) {
            L->stack_.closeUpvals(&base[A-1]);
          }
          ci->Jump(GETARG_sBx(i));
          break;
        }

      case OP_EQ:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];
          if (cast_int(luaV_equalobj_(L, rb, rc)) != GETARG_A(i)) {
            ci->Jump(1);
          }
          break;
        }

      case OP_LT:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];

          int result = luaV_lessthan(L, rb, rc);

          if (result != (int)A) ci->Jump(1);
          break;
        }

      case OP_LE:
        {
          LuaValue* rb = (B & 256) ? &k[Bk] : &base[Bk];
          LuaValue* rc = (C & 256) ? &k[Ck] : &base[Ck];

          int result = luaV_lessequal(L, rb, rc);

          if (result != (int)A) ci->Jump(1);
          break;
        }

      case OP_TEST:
        {
          bool isfalse = base[A].isFalse();

          if (isfalse == (C ? true : false)) ci->Jump(1);
          break;
        }

      case OP_TESTSET:
        {
          bool isfalse = base[B].isFalse();

          if (isfalse == (C ? true : false)) {
            ci->Jump(1);
          } else {
            base[A] = base[B];
          }
          break;
        }

      case OP_CALL:
        {
          int nargs = GETARG_B(i) - 1;
          int nresults = GETARG_C(i) - 1;

          // If nargs == -1, top is already set, so compute nargs from ra
          if (nargs >= 0) {
            L->stack_.top_ = ra + nargs + 1;
          }
          else {
            nargs = (L->stack_.top_ - ra) - 1;
          }

          int funcindex = L->stack_.topsize() - nargs - 1;
          LuaValue func = L->stack_[funcindex];

          result = luaD_precall2(L, funcindex, nresults);
          handleResult(result);

          if (func.isCallback() || func.isCClosure()) {  /* C function? */
            luaV_execute(L, funcindex, nresults);
            if (nresults >= 0) {
              L->stack_.top_ = ci->getTop();  /* adjust results */
            }
          }
          else {  /* Lua function */
            ci = L->stack_.callinfo_;
            ci->callstatus |= CIST_REENTRY;
          }
          break;
        }

      case OP_TAILCALL:
        {
          int nargs = GETARG_B(i) - 1;

          // If nargs == -1, top is already set, so compute nargs from ra
          if (nargs >= 0) {
            L->stack_.top_ = ra + nargs + 1;
          }
          else {
            nargs = (L->stack_.top_ - ra) - 1;
          }
          assert(GETARG_C(i) - 1 == LUA_MULTRET);

          int funcindex = L->stack_.topsize() - nargs - 1;

          result = luaD_precall2(L, funcindex, LUA_MULTRET);
          handleResult(result);

          LuaValue func = L->stack_[funcindex];
          if (!func.isCallback() && !func.isCClosure()) {
            /* tail call: put called frame (n) in place of caller one (o) */
            LuaStackFrame *nci = L->stack_.callinfo_;  /* called frame */
            LuaStackFrame *oci = nci->previous;  /* caller frame */
            StkId nfunc = nci->getFunc();  /* called function */
            StkId ofunc = oci->getFunc();  /* caller function */
            /* last stack slot filled by 'precall' */
            StkId lim = nci->getBase() + nfunc->getLClosure()->proto_->numparams;
            /* close all upvalues from previous call */
            if (cl->proto_->subprotos_.size() > 0) L->stack_.closeUpvals(oci->getBase());
            /* move new frame into old one */
            for (int aux = 0; nfunc + aux < lim; aux++) {
              ofunc[aux] = nfunc[aux];
            }
            oci->setBase( ofunc + (nci->getBase() - nfunc) );  /* correct base */
            L->stack_.top_ = ofunc + (L->stack_.top_ - nfunc);  /* correct top */
            oci->setTop( L->stack_.top_ );
            oci->resetPC();
            oci->callstatus |= CIST_TAIL;  /* function was tail called */
            ci = L->stack_.callinfo_ = oci;  /* remove new frame */
            assert(L->stack_.top_ == oci->getBase() + ofunc->getLClosure()->proto_->maxstacksize);
          }
          else {
            luaV_execute(L, funcindex, LUA_MULTRET);
          }
          break;
        }

      case OP_RETURN:
        {
          int nresults = GETARG_B(i) - 1;

          if(nresults == -1) {
            nresults = L->stack_.top_ - ra;
          }
          else {
             L->stack_.top_ = ra + nresults;
          }

          if (cl->proto_->subprotos_.size() > 0) {
            L->stack_.closeUpvals(base);
          }

          LuaStackFrame* old_frame = L->stack_.callinfo_;
          luaD_postcall(L, ra, nresults);
          LuaStackFrame* new_frame = L->stack_.callinfo_;

          if (!(old_frame->callstatus & CIST_REENTRY)) {  /* 'ci' still the called one */
            return RR_DONE;  /* external invocation: return */
          }
          else {  /* invocation via reentry: continue execution */
            if (old_frame->nresults >= 0) {
              L->stack_.top_ = new_frame->getTop();
            }
            assert(new_frame->getCurrentOp() == OP_CALL);
          }
          break;
        }

        // TODO(aappleby): What's the 'external index'?
        // OP_FORLOOP comes at the _end_ of a for block. It updates the loop
        // counter and jump back to the start of the loop if the counter has
        // not passed the limit.
      case OP_FORLOOP:
        {
          double index = base[A+0].getNumber();
          double limit = base[A+1].getNumber();
          double step  = base[A+2].getNumber();

          index += step;

          if ((step > 0) ? (index <= limit) : (index >= limit)) {
            ci->Jump(GETARG_sBx(i));  /* jump back */
            base[A+0] = index;  /* update internal index... */
            base[A+3] = index;  /* ...and external index */
          }
          break;
        }

      case OP_FORPREP:
        {
          LuaValue index = base[A+0].convertToNumber();
          LuaValue limit = base[A+1].convertToNumber();
          LuaValue step  = base[A+2].convertToNumber();

          if (index.isNone()) {
            result = luaG_runerror(LUA_QL("for") " initial value must be a number");
            handleResult(result);
          }
          if (limit.isNone()) {
            result = luaG_runerror(LUA_QL("for") " limit must be a number");
            handleResult(result);
          }
          if (step.isNone()) {
            result = luaG_runerror(LUA_QL("for") " step must be a number");
            handleResult(result);
          }

          base[A+0] = index.getNumber() - step.getNumber();
          base[A+1] = limit;
          base[A+2] = step;

          ci->Jump(GETARG_sBx(i));
          break;
        }
      
      case OP_TFORCALL:
        {
          StkId cb = ra + 3;  /* call base */
          cb[2] = ra[2];
          cb[1] = ra[1];
          cb[0] = ra[0];

          L->stack_.top_ = cb + 3;  /* func. + 2 args (state and index) */
          luaD_call(L, 2, C, 1);
          L->stack_.top_ = ci->getTop();
          
          break;
        }

      case OP_TFORLOOP:
        {
          if (ra[1].isNotNil()) {  /* continue loop? */
            ra[0] = ra[1];  /* save control variable */
            ci->Jump(GETARG_sBx(i));  /* jump back */
          }
          break;
        }

      case OP_SETLIST:
        {
          int n = GETARG_B(i);
          int c = GETARG_C(i);
          int last;
          LuaTable *h;
          if (n == 0) n = cast_int(L->stack_.top_ - ra) - 1;
          if (c == 0) {
            assert(ci->getNextOp() == OP_EXTRAARG);
            c = GETARG_Ax(ci->getNextInstruction());
            ci->Jump(1);
          }
          h = ra->getTable();
          last = ((c-1)*LFIELDS_PER_FLUSH) + n;
          
          // needs more space? pre-allocate it at once.
          // NOTE(aappleby): This is mostly a performance optimization, but the
          // nextvar.lua tests break if it's removed.
          if (last > (int)h->getArraySize()) {
            h->resize(last, (int)h->getHashSize());
          }

          // TODO(aappleby): we probably don't have to call barrierback every time through this loop
          for (; n > 0; n--) {
            h->set(LuaValue(last--), ra[n]);
            luaC_barrierback(h, ra[n]);
          }
          L->stack_.top_ = ci->getTop();  /* correct top (in case of previous open call) */
          break;
        }

      case OP_CLOSURE:
        {
          LuaProto *p = cl->proto_->subprotos_[GETARG_Bx(i)];
          LuaClosure *ncl = getcached(p, cl->ppupvals_, base);  /* cached closure */
          if (ncl == NULL)  /* no match? */
            pushclosure(L, p, cl->ppupvals_, base, &base[A]);  /* create a new one */
          else {
            base[A] = LuaValue(ncl);  /* push cashed closure */
          }
          break;
        }

      case OP_VARARG:
        {
          int b = B - 1;

          int n = cast_int(base - ci->getFunc()) - cl->proto_->numparams - 1;

          if (b < 0) {  /* B == 0? */
            b = n;  /* get all var. arguments */
            LuaResult result = L->stack_.reserve2(n);
            handleResult(result);
            base = ci->getBase();

            ra = RA(i);  /* previous call may change the stack */
            L->stack_.top_ = ra + n;
          }

          for (int j = 0; j < b; j++) {
            if (j < n) {
              ra[j] = base[j-n];
            }
            else {
              ra[j] = LuaValue::Nil();
            }
          }
          break;
        }

      case OP_EXTRAARG:
        {
          assert(0);
          break;
        }
    }
  }
}

void luaV_run (LuaThread *L) {
  luaV_run2(L);
}

void luaV_execute (LuaThread *L, int funcindex, int /*nresults*/) {
  LuaResult result = LUA_OK;

  ScopedCallDepth d(L);
  if(L->l_G->call_depth_ == LUAI_MAXCCALLS) {
    result = luaG_runerror("C stack overflow");
    handleResult(result);
  } else if (L->l_G->call_depth_ >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3))) {
    result = LUA_ERRERR;
    handleResult(result);
  }

  LuaValue f1 = L->stack_[funcindex];

  if(f1.isCClosure() || f1.isCallback()) {
    LuaCallback f = f1.isCallback() ? f1.getCallback() : f1.getCClosure()->cfunction_;
    int n = (*f)(L);  /* do the actual call */

    L->stack_.checkArgs(n);

    luaD_postcall(L, L->stack_.top_ - n, n);
  }
  else {
    luaV_run(L);
  }
}
