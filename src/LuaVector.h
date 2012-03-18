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
    T* newbuf = reinterpret_cast<T*>(default_alloc(sizeof(T) * newsize, 0));
    if(newbuf == NULL) luaD_throw(LUA_ERRMEM);;
    memcpy(newbuf, buf_, sizeof(T) * std::min(size_,newsize));
    default_free(buf_, sizeof(T) * size_, 0);
    buf_ = newbuf;
    size_ = newsize;
  }

  void clear ( void )
  {
    default_free(buf_, sizeof(T) * size_, 0);
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

  T* buf_;
  size_t size_;
};