#pragma once

#include <algorithm>
#include "lmem.h"

l_noret luaD_throw (int errcode);

template<class T>
class LuaVector {
public:

  LuaVector()
  : buf_(NULL),
    size_(0)
  {
  }

  void init()
  {
    buf_ = NULL;
    size_ = 0;
  }

  ~LuaVector()
  {
    clear();
  }
  
  T& operator [] ( size_t index ) 
  {
    assert(index >= 0);
    assert(index < size_);
    return buf_[index];
  }

  const T& operator [] ( size_t index ) const
  {
    assert(index >= 0);
    assert(index < size_);
    return buf_[index];
  }

  void resize ( size_t newsize )
  {
    if(newsize == 0)
    {
      clear();
      return;
    }
    T* newbuf = reinterpret_cast<T*>(luaM_alloc(sizeof(T) * newsize, LAP_VECTOR));
    if(newbuf == NULL) luaD_throw(LUA_ERRMEM);
    if(size_) {
      memcpy(newbuf, buf_, sizeof(T) * std::min(size_,newsize));
      luaM_free(buf_, sizeof(T) * size_, LAP_VECTOR);
    }
    buf_ = newbuf;
    size_ = newsize;
  }

  void clear ( void )
  {
    if(size_) luaM_free(buf_, sizeof(T) * size_, LAP_VECTOR);
    buf_ = NULL;
    size_ = 0;
  }

  void grow() 
  {
    resize(size_ ? size_ * 2 : 16);
  }

  bool empty() 
  {
    return (size_ == 0);
  }
  
  size_t size() const { return size_; }

  const T* begin() const { return &buf_[0]; }
  T* begin() { return &buf_[0]; }

  const T* end() const { return begin() + size_; }
  T* end() { return begin() + size_; }

  void swap ( LuaVector<T>& v ) {
    T* tempbuf = v.buf_;
    size_t tempsize = v.size_;
    v.buf_ = buf_;
    v.size_ = size_;
    buf_ = tempbuf;
    size_ = tempsize;
  }

  T* buf_;
  size_t size_;
};