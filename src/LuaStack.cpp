#include "LuaStack.h"

#include "LuaCallinfo.h"
#include "LuaClosure.h"
#include "LuaGlobals.h" // for thread_G->l_registry
#include "LuaUpval.h"

#include <assert.h>

//------------------------------------------------------------------------------

LuaStack::LuaStack() {
  top_ = NULL;
  callinfo_head_ = new LuaStackFrame();
  callinfo_head_->stack_ = this;
  callinfo_ = callinfo_head_;
  open_upvals_ = NULL;
}

LuaStack::~LuaStack() {
  assert(open_upvals_ == NULL);
  delete callinfo_head_;
  callinfo_head_ = NULL;
}

//------------------------------------------------------------------------------
// Set up our starting stack & callinfo.

void LuaStack::init() {
  assert(empty());
  resize_nocheck(BASIC_STACK_SIZE);

  top_ = begin();

  /* initialize first ci */
  LuaStackFrame* ci = callinfo_head_;
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
  LuaValue *oldstack = begin();

  // Resize the stack array. but do not check to see if we've exceeded
  // our memory limit.
  resize_nocheck(newsize);

  // Correct the stack top pointer.
  top_ = begin() + (top_ - oldstack);
  
  // Correct all stack references in open upvalues.
  for (LuaObject* up = open_upvals_; up != NULL; up = up->next_) {
    LuaUpvalue* uv = static_cast<LuaUpvalue*>(up);
    uv->v = (uv->v - oldstack) + begin();
  }
  
  // Correct all stack references in all active callinfos.
  /*
  for (LuaStackFrame* ci = callinfo_; ci != NULL; ci = ci->previous) {
    ci->setTop( (ci->getTop() - oldstack) + begin() );
    ci->setFunc( (ci->getFunc() - oldstack) + begin() );
    if ((ci->callstatus & CIST_LUA)) {
      ci->setBase( (ci->getBase() - oldstack) + begin() );
    }
  }
  */
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
  callinfo_ = callinfo_head_;
  LuaStackFrame *ci = callinfo_head_->next;
  while (ci != NULL) {
    LuaStackFrame* next = ci->next;
    luaM_free(ci);
    ci = next;
  }
  callinfo_head_->next = NULL;

  clear();
}

//------------------------------------------------------------------------------
// Resizes the stack so that it can hold at least 'size' more elements.

// Our new stack size should be either twice the current size,
// or enough to hold what's already on the stack plus the
// additional space - whichever's greater. Not more than
// LUAI_MAXSTACK though.

LuaResult LuaStack::grow2(int extrasize) {
  // Asking for more stack when we're already over the limit is  an error.
  if ((int)size() > LUAI_MAXSTACK) {
    return LUA_ERRERR;
  }

  // Asking for more space than could possibly fit on the stack is an error.
  int inuse = (int)(top_ - begin());
  int needed = inuse + extrasize + EXTRA_STACK;
  if (needed > LUAI_MAXSTACK) {  /* stack overflow? */
    realloc(ERRORSTACKSIZE);
    return LUA_ERRSTACK;
  }

  int newsize = std::max(2 * (int)size(), needed);
  newsize = std::min(newsize, LUAI_MAXSTACK);
  realloc(newsize);
  return LUA_OK;
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

int maxNewSize = 0;

LuaResult LuaStack::reserve2(int newsize) {
  if(newsize > maxNewSize) {
    maxNewSize = newsize;
  }

  if ((last() - top_) <= newsize) {
    if(newsize == 0) {
      int b = 0;
      b++;
    }
    return grow2(newsize);
  }

  return LUA_OK;
}

//------------------------------------------------------------------------------
// The amount of stack "in use" includes everything up to the current
// top of the stack _plus_ anything referenced by an active callinfo.

int LuaStack::countInUse() {
  LuaStackFrame *temp_ci;
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
// The first item in a stack frame is the closure for the current function.
// Negative stack indices are indexed from the stack top.
// Negative indices less than or equal to LUA_REGISTRYINDEX are special.

LuaValue LuaStack::at(int idx) {
  if (idx > 0) {
    LuaValue *o = callinfo_->getFunc() + idx;
    if (o >= top_) {
      assert(false);
      return LuaValue::None();
    }
    else return *o;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return top_[idx];
  }

  if (idx == LUA_REGISTRYINDEX) {
    return thread_G->l_registry;
  }


  // Callbacks have no upvals
  if (callinfo_->getFunc()->isCallback()) {
    // can't assert here, some test code is intentionally trying to do this and
    // expecting to fail.
    //assert(false);
    return LuaValue::None();
  }

  idx = LUA_REGISTRYINDEX - idx - 1;

  LuaClosure* func = callinfo_->getFunc()->getCClosure();
  if(idx < func->nupvalues) {
    return func->pupvals_[idx];
  }

  // Invalid stack index.
  assert(false);
  return LuaValue::None();
}

LuaValue LuaStack::at_frame(int idx) {
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
  LuaValue v = at(index);
  if(v.isNone()) v = LuaValue::Nil();
  push(v);
}

void LuaStack::copy_frame(int index) {
  push(at_frame(index));
}

//------------------------------------------------------------------------------

void LuaStack::push(LuaValue v) {
  top_[0] = v;
  top_++;
  assert((top_ <= callinfo_->getTop()) && "stack overflow");
}

void LuaStack::push(const LuaValue* v) {
  top_[0] = *v;
  top_++;
  assert((top_ <= callinfo_->getTop()) && "stack overflow");
}

LuaResult LuaStack::push_reserve2(LuaValue v) {
  top_[0] = v;
  top_++;
  return reserve2(0);
}

void LuaStack::push_nocheck(LuaValue v) {
  top_[0] = v;
  top_++;
}

LuaValue LuaStack::pop() {
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
  LuaValue* p = (index > 0) ? &callinfo_->getFunc()[index] : &top_[index];
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
      push(LuaValue::Nil());
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

LuaStackFrame* LuaStack::extendCallinfo() {
  LuaStackFrame *ci = new LuaStackFrame();
  ci->stack_ = this;
  assert(callinfo_->next == NULL);
  callinfo_->next = ci;
  ci->previous = callinfo_;
  ci->next = NULL;
  return ci;
}

LuaStackFrame* LuaStack::nextCallinfo() {
  if(callinfo_->next == NULL) {
    callinfo_ = extendCallinfo();
  } else {
    callinfo_ = callinfo_->next;
  }
  return callinfo_;
}

void LuaStack::sweepCallinfo() {
  LuaStackFrame *ci = callinfo_;
  LuaStackFrame *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    delete ci;
  }
}

//------------------------------------------------------------------------------

LuaUpvalue* LuaStack::createUpvalFor(StkId level) {
  LuaObject **pp = &open_upvals_;
  LuaUpvalue *p;
  LuaUpvalue *uv;
  while (*pp != NULL && (p = dynamic_cast<LuaUpvalue*>(*pp))->v >= level) {
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
  uv = new LuaUpvalue(pp);
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
  LuaUpvalue *uv;

  while (open_upvals_ != NULL) {
    uv = dynamic_cast<LuaUpvalue*>(open_upvals_);
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

          LuaGCVisitor visitor(&thread_G->gc_);
          visitor.MarkValue(*uv->v);
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

LuaStackFrame* LuaStack::findProtectedCall() {
  LuaStackFrame *ci;
  for (ci = callinfo_; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}

LuaResult LuaStack::createCCall2(StkId func, int nresults, int nstack) {
  // ensure minimum stack size
  ptrdiff_t func_index = func - begin();
  LuaResult result = reserve2(nstack);
  if(result != LUA_OK) return result;
  func = begin() + func_index;

  LuaStackFrame* ci = nextCallinfo();  /* now 'enter' new function */
  ci->nresults = nresults;
  ci->setFunc(func);
  ci->setTop(top_ + nstack);
  assert(ci->getTop() <= last());
  ci->callstatus = 0;
  return LUA_OK;
}

//------------------------------------------------------------------------------

void LuaStack::sanityCheck() {

  // All open upvals must be open and must be non-black.
  for (LuaObject* uvo = open_upvals_; uvo != NULL; uvo = uvo->next_) {
    LuaUpvalue *uv = dynamic_cast<LuaUpvalue*>(uvo);
    assert(uv->v != &uv->value);
    assert(!uvo->isBlack());
  }

  for (LuaStackFrame* ci = callinfo_; ci != NULL; ci = ci->previous) {
    assert(ci->getTop() <= last());
    ci->sanityCheck();
  }

  for(int i = 0; i < (int)size(); i++) {
    LuaValue* v = &buf_[i];
    if(v == top_) break;
    v->sanityCheck();
  }
}

//------------------------------------------------------------------------------
