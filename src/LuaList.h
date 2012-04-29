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

  LuaObject* getHead() { return head_; }
  LuaObject* getTail() { return tail_; }

  void Push(LuaObject* o) {
    assert(o->getPrev() == NULL);
    assert(o->getNext() == NULL);

    if(head_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      o->setNext(head_);
      head_->setPrev(o);
      head_ = o;
    }
  }

  void PushTail(LuaObject* o) {
    assert(o->getNext() == NULL);

    if(tail_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      tail_->setNext(o);
      tail_ = o;
    }
  }

  LuaObject* Pop() {
    if(head_ == NULL) {
      return NULL;
    }

    LuaObject* o = head_;
    Detach(o);
    return o;
  }

  void Detach(LuaObject* o) {
    if(o == NULL) return;

    if(head_ == o) head_ = o->getNext();
    if(tail_ == o) tail_ = o->getPrev();

    if(o->getPrev()) o->getPrev()->setNext(o->getNext());
    if(o->getNext()) o->getNext()->setPrev(o->getPrev());

    o->setPrev(NULL);
    o->setNext(NULL);
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
    for(LuaObject* o = head_; o; o = o->getNext()) {
      c(o);
    }
  }

  class iterator {
  public:
    iterator()
      : list_(NULL), object_(NULL) {
    }

    iterator(LuaList* list)
      : list_(list), object_(list->getHead()) {
    }

    LuaObject* get() { return object_; }

    LuaObject* operator -> () {
      return object_;
    }

    operator bool() {
      return object_ != NULL;
    }

    iterator& operator ++() {
      object_ = object_->getNext();
      return *this;
    }

    operator LuaObject* () {
      return object_;
    }

    void abort() {
      object_ = NULL;
    }

    // Removes the object at the iterator from the list and moves to the next
    // item in the list.
    LuaObject* pop() {
      if(object_ == NULL) return NULL;

      LuaObject* old = object_;
      object_ = object_->getNext();
      list_->Detach(old);
      return old;
    }

  protected:
    LuaList* list_;
    LuaObject* object_;
  };

  iterator begin() {
    return iterator(this);
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
    assert(o->getNextGray() == NULL);
    o->setColor(LuaObject::GRAY);

    if(head_ == NULL) {
      head_ = o;
      tail_ = o;
    } else {
      o->setNextGray(head_);
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
    head_ = o->getNextGray();
    o->setNextGray(NULL);
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
    for(LuaObject* o = head_; o; o = o->getNextGray()) {
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