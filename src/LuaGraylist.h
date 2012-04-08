#pragma once

#include "LuaObject.h"

#include <assert.h>

class LuaGraylist {
public:

  LuaGraylist() {
    head_ = NULL;
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
  void PropagateGC(GCVisitor& visitor) {
    while(head_) {
      LuaObject *o = Pop();
      o->PropagateGC(visitor);
    }
  }

  void Sweep();
  void SweepKeys();
  void SweepValues();

  LuaObject* head_;
  LuaObject* tail_;
};