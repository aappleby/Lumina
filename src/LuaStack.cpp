#include "LuaStack.h"

#include "LuaClosure.h"
#include "LuaGlobals.h" // for thread_G->l_registry
#include "LuaUpval.h"

#include <assert.h>

l_noret luaG_runerror (const char *fmt, ...);

//------------------------------------------------------------------------------
// Set up our starting stack & callinfo.

void LuaStack::init() {
  assert(empty());
  resize_nocheck(BASIC_STACK_SIZE);

  top_ = begin();

  /* initialize first ci */
  CallInfo* ci = &callinfo_head_;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->setFunc(getTop());
  top_++;
  ci->setTop(getTop() + LUA_MINSTACK);
}

//------------------------------------------------------------------------------
// Resizes the stack and fixes up all pointers into the stack so they refer
// to the correct locations.

void LuaStack::realloc (int newsize) {
  assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);

  // Remember where the old stack was. This will be a dangling pointer
  // after the resize, but that's OK as we only use it to fix up the
  // existing pointers - it doesn't get dereferenced.
  TValue *oldstack = begin();

  // Resize the stack array. but do not check to see if we've exceeded
  // our memory limit.
  resize_nocheck(newsize);

  // Correct the stack top pointer.
  top_ = begin() + (top_ - oldstack);
  
  // Correct all stack references in open upvalues.
  for (LuaObject* up = open_upvals_; up != NULL; up = up->next_) {
    UpVal* uv = static_cast<UpVal*>(up);
    uv->v = (uv->v - oldstack) + begin();
  }
  
  // Correct all stack references in all active callinfos.
  /*
  for (CallInfo* ci = callinfo_; ci != NULL; ci = ci->previous) {
    ci->setTop( (ci->getTop() - oldstack) + begin() );
    ci->setFunc( (ci->getFunc() - oldstack) + begin() );
    if ((ci->callstatus & CIST_LUA)) {
      ci->setBase( (ci->getBase() - oldstack) + begin() );
    }
  }
  */

  // Stack is valid again, _now_ kick off memory errors if we're over the
  // limit.
  l_memcontrol.checkLimit();
}

//------------------------------------------------------------------------------
// Unwind all callinfos and deallocate the stack.

void LuaStack::free() {
  if (empty()) {
    // Stack not completely built yet - we probably ran out of memory while
    // trying to create a thread.
    return;  
  }
  
  // free the entire 'ci' list
  callinfo_ = &callinfo_head_;
  CallInfo *ci = callinfo_head_.next;
  while (ci != NULL) {
    CallInfo* next = ci->next;
    luaM_free(ci);
    ci = next;
  }
  callinfo_head_.next = NULL;

  clear();
}

//------------------------------------------------------------------------------
// Resizes the stack so that it can hold at least 'size' more elements.

// Our new stack size should be either twice the current size,
// or enough to hold what's already on the stack plus the
// additional space - whichever's greater. Not more than
// LUAI_MAXSTACK though.

void LuaStack::grow(int extrasize) {
  // Asking for more stack when we're already over the limit is  an error.
  if ((int)size() > LUAI_MAXSTACK)  /* error after extra size? */
    luaD_throw(LUA_ERRERR);

  // Asking for more space than could possibly fit on the stack is an error.
  int inuse = (int)(top_ - begin());
  int needed = inuse + extrasize + EXTRA_STACK;
  if (needed > LUAI_MAXSTACK) {  /* stack overflow? */
    realloc(ERRORSTACKSIZE);
    luaG_runerror("stack overflow");
  }

  int newsize = std::max(2 * (int)size(), needed);
  newsize = std::min(newsize, LUAI_MAXSTACK);
  realloc(newsize);
}


//------------------------------------------------------------------------------
// Reduce the size of the stack to ~125% of what's necessary to store
// everything currently in use.

void LuaStack::shrink() {
  size_t inuse = countInUse();
  size_t goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (inuse > LUAI_MAXSTACK || goodsize >= size()) {
  } else {
    realloc((int)goodsize);  /* shrink it */
  }
}

//------------------------------------------------------------------------------

void LuaStack::reserve(int newsize) {
  if ((last() - top_) <= newsize) grow(newsize);
}

//------------------------------------------------------------------------------
// The amount of stack "in use" includes everything up to the current
// top of the stack _plus_ anything referenced by an active callinfo.

int LuaStack::countInUse() {
  CallInfo *temp_ci;
  StkId lim = top_;
  for (temp_ci = callinfo_; temp_ci != NULL; temp_ci = temp_ci->previous) {
    assert(temp_ci->getTop() <= last());
    if (lim < temp_ci->getTop()) {
      lim = temp_ci->getTop();
    }
  }
  return (int)(lim - begin()) + 1;  /* part of stack in use */
}

//------------------------------------------------------------------------------
// Positive stack indices are indexed from the current call frame.
// The first item in a call frame is the closure for the current function.
// Negative stack indices are indexed from the stack top.
// Negative indices less than or equal to LUA_REGISTRYINDEX are special.

TValue LuaStack::at(int idx) {
  if (idx > 0) {
    TValue *o = callinfo_->getFunc() + idx;
    if (o >= top_) {
      assert(false);
      return TValue::None();
    }
    else return *o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return top_[idx];
  }

  if (idx == LUA_REGISTRYINDEX) {
    return thread_G->l_registry;
  }


  // Light C functions have no upvals
  if (callinfo_->getFunc()->isLightFunction()) {
    // can't assert here, some test code is intentionally trying to do this and
    // expecting to fail.
    //assert(false);
    return TValue::None();
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  Closure* func = callinfo_->getFunc()->getCClosure();
  if(idx < func->nupvalues) {
    return func->pupvals_[idx];
  }

  // Invalid stack index.
  assert(false);
  return TValue::None();
}

TValue LuaStack::at_frame(int idx) {
  assert(idx > LUA_REGISTRYINDEX);
  assert(idx < (top_ - callinfo_->getFunc()));

  if (idx > 0) {
    return callinfo_->getFunc()[idx];
  }
  else {
    return top_[idx];
  }
}

//------------------------------------------------------------------------------

void LuaStack::copy(int index) {
  TValue v = at(index);
  if(v.isNone()) v = TValue::Nil();
  push(v);
}

void LuaStack::copy_frame(int index) {
  push(at_frame(index));
}

//------------------------------------------------------------------------------

void LuaStack::push(TValue v) {
  top_[0] = v;
  top_++;
  assert((top_ <= callinfo_->getTop()) && "stack overflow");
}

void LuaStack::push(const TValue* v) {
  top_[0] = *v;
  top_++;
  assert((top_ <= callinfo_->getTop()) && "stack overflow");
}

void LuaStack::push_reserve(TValue v) {
  top_[0] = v;
  top_++;
  reserve(0);
}

void LuaStack::push_nocheck(TValue v) {
  top_[0] = v;
  top_++;
}

TValue LuaStack::pop() {
  top_--;
  return *top_;
}

void LuaStack::pop(int count) {
  top_ -= count;
}

// Moves the item on the top of the stack to 'idx'.
// TODO(aappleby): Not sure if this works in all cases, and it would
// be better if it took the value as an arg...

/*
void LuaStack::insert(int idx) {
  THREAD_CHECK(L);
  StkId p;
  StkId q;
  p = index2addr_checked(L, idx);
  for (q = top_; q>p; q--) {
    q[0] = q[-1];
  }
  p[0] = top_[0];
}
*/

void LuaStack::remove(int index) {
  assert(index > LUA_REGISTRYINDEX);
  TValue* p = (index > 0) ? &callinfo_->getFunc()[index] : &top_[index];
  while (++p < top_) {
    p[-1] = p[0];
  }
  top_--;
}

//------------------------------------------------------------------------------

int LuaStack::getTopIndex() {
  return (int)(top_ - callinfo_->getFunc()) - 1;
}


void LuaStack::setTopIndex(int idx) {
  StkId func = callinfo_->getFunc();

  if (idx >= 0) {
    assert((idx <= last() - (func + 1)) && "new top too large");
    while (top_ < (func + 1) + idx) {
      push(TValue::nil);
    }
    top_ = (func + 1) + idx;
  }
  else {
    assert((-(idx+1) <= (top_ - (func + 1))) && "invalid new top");
    top_ += idx+1;  /* `subtract' index (index is negative) */
  }
}

//------------------------------------------------------------------------------

void LuaStack::checkArgs(int count) {
  int actual = (int)(top_ - callinfo_->getFunc()) - 1;
  assert((count <= actual) && "not enough elements in the stack");
}

//------------------------------------------------------------------------------

CallInfo* LuaStack::extendCallinfo() {
  CallInfo *ci = new CallInfo();
  if(ci == NULL) luaD_throw(LUA_ERRMEM);

  ci->stack_ = this;
  assert(callinfo_->next == NULL);
  callinfo_->next = ci;
  ci->previous = callinfo_;
  ci->next = NULL;
  return ci;
}

CallInfo* LuaStack::nextCallinfo() {
  if(callinfo_->next == NULL) {
    callinfo_ = extendCallinfo();
  } else {
    callinfo_ = callinfo_->next;
  }
  return callinfo_;
}

void LuaStack::sweepCallinfo() {
  CallInfo *ci = callinfo_;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    delete ci;
  }
}

//------------------------------------------------------------------------------

UpVal* LuaStack::createUpvalFor(StkId level) {
  LuaObject **pp = &open_upvals_;
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL && (p = dynamic_cast<UpVal*>(*pp))->v >= level) {
    assert(p->v != &p->value);
    if (p->v == level) {
      // Resurrect the upvalue if necessary.
      // TODO(aappleby): The upval is supposedly on the stack, how in the heck
      // could it be dead?
      if (p->isDead()) {
        p->makeLive();
      }
      return p;
    }
    p->clearOld();  /* may create a newer upval after this one */
    pp = &(p->next_);
  }
  /* not found: create a new one */
  uv = new UpVal(pp);
  uv->v = level;  /* current value lives in the stack */

  // TODO(aappleby): Is there any way to break this dependency on the global state?  
  uv->uprev = &thread_G->uvhead;  /* double link it in `uvhead' list */
  uv->unext = thread_G->uvhead.unext;
  uv->unext->uprev = uv;
  thread_G->uvhead.unext = uv;

  assert(uv->unext->uprev == uv && uv->uprev->unext == uv);
  return uv;
}

void LuaStack::closeUpvals(StkId level) {
  UpVal *uv;

  while (open_upvals_ != NULL) {
    uv = dynamic_cast<UpVal*>(open_upvals_);
    if(uv->v < level) break;

    assert(!uv->isBlack() && uv->v != &uv->value);
    open_upvals_ = uv->next_;  /* remove from `open' list */

    if (uv->isDead())
      delete uv;
    else {
      uv->unlink();  /* remove upvalue from 'uvhead' list */

      uv->value = *uv->v;  /* move value to upvalue slot */
      uv->v = &uv->value;  /* now current value lives here */
      
      uv->next_ = thread_G->allgc;  /* link upvalue into 'allgc' list */
      thread_G->allgc = uv;

      // check color (and invariants) for an upvalue that was closed,
      // i.e., moved into the 'allgc' list
      // open upvalues are never black
      assert(!uv->isBlack());

      if (uv->isGray()) {
        if (thread_G->keepInvariant()) {
          uv->clearOld();  /* see MOVE OLD rule */
          uv->grayToBlack();  /* it is being visited now */
          thread_G->markValue(uv->v);
        }
        else {
          assert(thread_G->isSweepPhase());
          uv->makeLive();
        }
      }
    }
  }
}

//------------------------------------------------------------------------------

CallInfo* LuaStack::findProtectedCall() {
  CallInfo *ci;
  for (ci = callinfo_; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}

void LuaStack::createCCall(StkId func, int nresults, int nstack)
{
  // ensure minimum stack size
  ptrdiff_t func_index = func - begin();
  reserve(nstack);
  func = begin() + func_index;

  ScopedMemChecker c;
  CallInfo* ci = nextCallinfo();  /* now 'enter' new function */
  ci->nresults = nresults;
  ci->setFunc(func);
  ci->setTop(top_ + nstack);
  assert(ci->getTop() <= last());
  ci->callstatus = 0;
}

//------------------------------------------------------------------------------

void LuaStack::sanityCheck() {

  // All open upvals must be open and must be non-black.
  for (LuaObject* uvo = open_upvals_; uvo != NULL; uvo = uvo->next_) {
    UpVal *uv = dynamic_cast<UpVal*>(uvo);
    assert(uv->v != &uv->value);
    assert(!uvo->isBlack());
  }

  for (CallInfo* ci = callinfo_; ci != NULL; ci = ci->previous) {
    assert(ci->getTop() <= last());
    ci->sanityCheck();
  }

  for(int i = 0; i < (int)size(); i++) {
    TValue* v = &buf_[i];
    if(v == top_) break;
    v->sanityCheck();
  }
}

//------------------------------------------------------------------------------
