#pragma once

#include "LuaObject.h"

#include <assert.h>

//------------------------------------------------------------------------------

class LuaList {
public:

  LuaList() {
    head_ = NULL;
    tail_ = NULL;
  }

  ~LuaList() {
    assert(isEmpty());
  }

  void Push(LuaObject* o) {
    assert(o->next_ == NULL);

    if(head_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      o->next_ = head_;
      head_ = o;
    }
  }

  void PushTail(LuaObject* o) {
    assert(o->next_ == NULL);

    if(tail_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      tail_->next_ = o;
      tail_ = o;
    }
  }

  LuaObject* Pop() {
    if(head_ == NULL) {
      return NULL;
    }

    if(head_ == tail_) {
      LuaObject* o = head_;
      head_ = NULL;
      tail_ = NULL;
      return o;
    }

    LuaObject* o = head_;
    head_ = o->next_;
    o->next_ = NULL;
    return o;
  }

  void Swap(LuaList& l) {
    LuaObject* temp_head = head_;
    LuaObject* temp_tail = tail_;
    head_ = l.head_;
    tail_ = l.tail_;
    l.head_ = temp_head;
    l.tail_ = temp_tail;
  }

  bool isEmpty() { 
    return head_ == NULL;
  }

  void Clear() {
    while(head_) Pop();
  }

  typedef void (*traverseCB)(LuaObject* o);

  void Traverse(traverseCB c) {
    for(LuaObject* o = head_; o; o = o->next_) {
      c(o);
    }
  }

  class iterator {
  public:
    iterator(LuaObject* o) : object_(o) {
    }

    LuaObject* operator -> () {
      return object_;
    }

    operator bool() {
      return object_ != NULL;
    }

    iterator& operator ++() {
      object_ = object_->next_;
      return *this;
    }

    operator LuaObject* () {
      return object_;
    }

  protected:
    LuaObject* object_;
  };

  iterator begin() {
    return iterator(head_);
  }

protected:
  LuaObject* head_;
  LuaObject* tail_;
};

//------------------------------------------------------------------------------

class LuaGraylist {
public:

  LuaGraylist() {
    head_ = NULL;
    tail_ = NULL;
  }

  ~LuaGraylist() {
    assert(isEmpty());
  }

  void Swap(LuaGraylist& l) {
    LuaObject* temp_head = head_;
    LuaObject* temp_tail = tail_;
    head_ = l.head_;
    tail_ = l.tail_;
    l.head_ = temp_head;
    l.tail_ = temp_tail;
  }

  void Push(LuaObject* o) {
    assert(o->next_gray_ == NULL);
    o->setColor(LuaObject::GRAY);

    if(head_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      o->next_gray_ = head_;
      head_ = o;
    }
  }

  LuaObject* Pop() {
    if(head_ == NULL) {
      return NULL;
    }

    if(head_ == tail_) {
      LuaObject* o = head_;
      head_ = NULL;
      tail_ = NULL;
      return o;
    }

    LuaObject* o = head_;
    head_ = o->next_gray_;
    o->next_gray_ = NULL;
    return o;
  }

  bool isEmpty() { 
    return head_ == NULL;
  }

  void Clear();

  // Propagate marks through all objects on this graylist, removing them
  // from the list as we go.
  void PropagateGC(LuaGCVisitor& visitor) {
    while(head_) {
      LuaObject *o = Pop();
      o->PropagateGC(visitor);
    }
  }

  typedef void (*traverseCB)(LuaObject* o);

  void Traverse(traverseCB c) {
    for(LuaObject* o = head_; o; o = o->next_gray_) {
      c(o);
    }
  }

  void Sweep();
  void SweepKeys();
  void SweepValues();

private:
  LuaObject* head_;
  LuaObject* tail_;
};